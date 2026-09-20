#include "app_leds.h"

#include <string.h>

#include "app_pins.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

static const char *TAG = "leds";

// 10 MHz, so one tick is 0.1 us and the WS2812 bit times are whole ticks.
#define RESOLUTION_HZ 10000000

// WS2812B: a 0 bit is a short high then a long low, a 1 bit the reverse, and
// the whole bit is 1.25 us. These are the widely compatible values rather than
// the datasheet's nominal ones - the parts on this guitar are unmarked, so the
// timings that work across the clones matter more than the ones in a PDF.
#define T0H 3  // 0.3 us
#define T0L 9  // 0.9 us
#define T1H 9  // 0.9 us
#define T1L 3  // 0.3 us

// No reset symbol is encoded. WS2812 latches after about 50 us of idle line,
// and at 60 Hz there are more than sixteen milliseconds between frames. The
// gap is the latch.
#define MAX_LEDS 128

typedef struct {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    int leds;
    uint8_t grb[MAX_LEDS * 3];
} strip_t;

static strip_t s_strips[EL_STRIP_COUNT];
static bool s_ready;

static const int STRIP_PINS[EL_STRIP_COUNT] = {
    PIN_LED_BASS, PIN_LED_TREBLE, PIN_ONBOARD_LED,
};

static esp_err_t init_strip(strip_t *s, int gpio, int leds)
{
    rmt_tx_channel_config_t ch = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RESOLUTION_HZ,
        // One block. The ESP32-S3 has four TX channels sharing 4 x 48 symbols,
        // and asking for 64 rounds up to two blocks each - so three channels
        // wanted six blocks out of four and the third one simply failed to
        // allocate. 48 symbols is 60 us of buffered line at WS2812 bit rate,
        // which the driver refills from an interrupt long before it runs dry.
        .mem_block_symbols = 48,
        .trans_queue_depth = 2,
    };
    esp_err_t err = rmt_new_tx_channel(&ch, &s->channel);
    if (err != ESP_OK) return err;

    rmt_bytes_encoder_config_t enc = {
        .bit0 = { .level0 = 1, .duration0 = T0H, .level1 = 0, .duration1 = T0L },
        .bit1 = { .level0 = 1, .duration0 = T1H, .level1 = 0, .duration1 = T1L },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&enc, &s->encoder);
    if (err != ESP_OK) return err;

    s->leds = leds;
    memset(s->grb, 0, sizeof s->grb);
    return rmt_enable(s->channel);
}

esp_err_t app_leds_init(int leds_per_strip)
{
    if (leds_per_strip < 1 || leds_per_strip > MAX_LEDS) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < EL_STRIP_COUNT; i++) {
        const int leds = (i == EL_STRIP_ONBOARD) ? 1 : leds_per_strip;
        esp_err_t err = init_strip(&s_strips[i], STRIP_PINS[i], leds);
        if (err == ESP_OK) continue;

        ESP_LOGE(TAG, "strip %d on GPIO%d: %s", i, STRIP_PINS[i], esp_err_to_name(err));
        // The board's own LED is a convenience; the neck is the instrument.
        // Losing the first to a resource the second needs is a bad trade, and
        // taking the whole render loop down with it is a worse one - that is
        // what happened the first time this ran on hardware.
        if (i == EL_STRIP_ONBOARD) {
            s_strips[i].channel = NULL;
            continue;
        }
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "%d LEDs per strip on GPIO%d and GPIO%d, plus the board's own on GPIO%d",
             leds_per_strip, PIN_LED_BASS, PIN_LED_TREBLE, PIN_ONBOARD_LED);
    return ESP_OK;
}

static esp_err_t begin(strip_t *s)
{
    const rmt_transmit_config_t cfg = { .loop_count = 0 };
    return rmt_transmit(s->channel, s->encoder, s->grb, (size_t)s->leds * 3, &cfg);
}

static esp_err_t finish(strip_t *s)
{
    return rmt_tx_wait_all_done(s->channel, 100);
}

static esp_err_t send(strip_t *s)
{
    esp_err_t err = begin(s);
    return err != ESP_OK ? err : finish(s);
}

esp_err_t app_leds_write(const uint8_t *rgb, int total_leds)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    const int per_strip = total_leds / 2;
    for (int side = 0; side < 2; side++) {
        strip_t *s = &s_strips[side];
        const int n = per_strip < s->leds ? per_strip : s->leds;
        const uint8_t *src = rgb + (size_t)side * per_strip * 3;
        // The engine speaks RGB; the part wants GRB.
        for (int i = 0; i < n; i++) {
            s->grb[i * 3 + 0] = src[i * 3 + 1];
            s->grb[i * 3 + 1] = src[i * 3 + 0];
            s->grb[i * 3 + 2] = src[i * 3 + 2];
        }
        for (int i = n; i < s->leds; i++) {
            s->grb[i * 3 + 0] = 0; s->grb[i * 3 + 1] = 0; s->grb[i * 3 + 2] = 0;
        }
    }

    // Both strips on the wire at once, then wait for both. They are separate
    // RMT channels, so serialising them would double the 780 us a 26-LED strip
    // takes for no reason - and that is real money against a 16.7 ms frame.
    esp_err_t a = begin(&s_strips[EL_STRIP_BASS]);
    esp_err_t b = begin(&s_strips[EL_STRIP_TREBLE]);
    if (a == ESP_OK) a = finish(&s_strips[EL_STRIP_BASS]);
    if (b == ESP_OK) b = finish(&s_strips[EL_STRIP_TREBLE]);
    return a != ESP_OK ? a : b;
}

esp_err_t app_leds_onboard(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    strip_t *s = &s_strips[EL_STRIP_ONBOARD];
    if (!s->channel) return ESP_ERR_NOT_SUPPORTED;
    s->grb[0] = g; s->grb[1] = r; s->grb[2] = b;
    return send(s);
}

void app_leds_blank(void)
{
    if (!s_ready) return;
    for (int i = 0; i < EL_STRIP_COUNT; i++) {
        if (!s_strips[i].channel) continue;
        memset(s_strips[i].grb, 0, sizeof s_strips[i].grb);
        send(&s_strips[i]);
    }
}
