// Compile the real renderer, upload handler and boot orchestration. Only ESP
// services are fake; malformed JSON and bytecode go through the real decoders.
#include <assert.h>
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "app_config.h"
#include "app_mode.h"
#include "app_inputs.h"
#include "app_render.h"
#include "app_effects.h"
#include "app_selftest.h"
#include "esp_app_desc.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static app_config_t config;
static el_radio_mode_t radio;
static bool reachable = true;
static bool confirmed;
static uint32_t ticks;
static void (*render_fn)(void *);
static jmp_buf done;
static int frames, stop_after;
static int physical = -1;
static bool audition;
static char saved[8192];
static size_t saved_len;
static bool battery_valid = true;
static float pin_volts = 1.9f;

void test_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
const char *esp_err_to_name(esp_err_t e) { (void)e; return "test error"; }
size_t strlcpy(char *d,const char *s,size_t n) { size_t l=strlen(s); if(n) {size_t c=l<n-1?l:n-1;memcpy(d,s,c);d[c]=0;}return l; }
int64_t esp_timer_get_time(void) { return (int64_t)ticks*1000; }
TickType_t xTaskGetTickCount(void) { return ticks; }
void vTaskDelay(TickType_t t) {
    ticks += t;
    if (!stop_after) return;
    frames++;
    if (audition && frames == 2) app_render_select(3);
    if (frames >= stop_after) longjmp(done,1);
}
int xTaskCreatePinnedToCore(void (*f)(void *),const char *n,unsigned st,void *a,int p,void *h,int c) {
    (void)n;(void)st;(void)a;(void)p;(void)h;(void)c;render_fn=f;return pdPASS;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void*)1; }
int xSemaphoreTake(SemaphoreHandle_t s,TickType_t t) { (void)s;(void)t;return pdPASS; }
void xSemaphoreGive(SemaphoreHandle_t s) { (void)s; }
esp_err_t app_inputs_init(void) { return ESP_OK; }
void app_inputs_read(app_inputs_t *out) { *out=(app_inputs_t){.position=physical,.brightness=1}; }
bool app_inputs_battery(float *v) { *v=pin_volts;return battery_valid; }
esp_err_t app_leds_init(int n) { (void)n;return ESP_OK; }
esp_err_t app_leds_write(const uint8_t *rgb,int n) { (void)rgb;(void)n;return ESP_OK; }
esp_err_t app_leds_onboard(uint8_t r,uint8_t g,uint8_t b) { (void)r;(void)g;(void)b;return ESP_OK; }
const app_config_t *app_config_get(void) { return &config; }
esp_err_t app_config_init(void) { return ESP_OK; }
esp_err_t app_wifi_start(el_radio_mode_t m) { (void)m; return ESP_OK; }
bool app_wifi_has_ip(void) { return reachable; }
esp_err_t app_http_start(void) { return ESP_OK; }
el_radio_mode_t app_mode_current(void) { return radio; }
el_radio_mode_t app_mode_decide(void) { return radio; }
void app_mode_mark_healthy(void) {}
void app_log_init(void) {}
bool app_ota_pending_verify(void) { return true; }
void app_ota_confirm_if_healthy(void) { confirmed = true; }
const char *app_ota_running_slot(void) { return "ota_0"; }
bool app_selftest_is_new_build(void) { return false; }
const app_selftest_t *app_selftest_run(void) { return NULL; }
const esp_app_desc_t *esp_app_get_description(void) { static const esp_app_desc_t d={"test","test"};return &d; }
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *h) { (void)n;(void)m;*h=1;return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *b,size_t n) { (void)h;(void)k;CHECK(n<sizeof saved);memcpy(saved,b,n);saved_len=n;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *b,size_t *n) { (void)h;(void)k;if(!saved_len)return ESP_ERR_NOT_FOUND;if(b)memcpy(b,saved,saved_len);*n=saved_len;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
void app_main(void);

static void render_frames(int n)
{
    stop_after=n;frames=0;
    if (!setjmp(done)) render_fn(NULL);
    stop_after=0;
}
static esp_err_t upload(cJSON *root)
{
    char why[160];char *json=cJSON_PrintUnformatted(root);
    esp_err_t ret=app_effects_apply(json,strlen(json),why,sizeof why);
    free(json);return ret;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);
    char payload[8192];size_t n=fread(payload,1,sizeof(payload)-1,f);fclose(f);payload[n]=0;
    config.battery=EL_BATTERY_DEFAULT;
    CHECK(app_render_start()==ESP_OK);
    cJSON *root=cJSON_Parse(payload);CHECK(root!=NULL);
    CHECK(upload(root)==ESP_OK);
    char original[8192];memcpy(original,saved,saved_len);size_t original_len=saved_len;
    el_output_t before,after;app_render_get_output(&before);
    cJSON *output=cJSON_GetObjectItem(root,"output");
    cJSON_SetNumberValue(cJSON_GetObjectItem(output,"brightnessCeiling"),0);
    cJSON *slot=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"slots"),4);
    cJSON_ReplaceItemInObject(slot,"program",cJSON_CreateString("RUxGWAE=")); // valid base64, invalid ELFX
    CHECK(upload(root)!=ESP_OK);
    app_render_get_output(&after);CHECK(after.brightness_ceiling==before.brightness_ceiling);
    CHECK(saved_len==original_len && memcmp(saved,original,saved_len)==0);
    cJSON_Delete(root);

    root=cJSON_Parse(payload);output=cJSON_GetObjectItem(root,"output");
    cJSON_SetNumberValue(cJSON_GetObjectItem(output,"mAPerLed"),1);
    CHECK(upload(root)!=ESP_OK);
    cJSON_SetNumberValue(cJSON_GetObjectItem(output,"mAPerLed"),60);
    cJSON_SetNumberValue(cJSON_GetObjectItem(output,"currentBudget"),20000);
    CHECK(upload(root)!=ESP_OK);
    cJSON_Delete(root);

    // Saving inherited output must reproduce the same state after a power cycle.
    root=cJSON_Parse(payload);output=cJSON_GetObjectItem(root,"output");
    cJSON_SetNumberValue(cJSON_GetObjectItem(output,"brightnessCeiling"),0.3);
    CHECK(upload(root)==ESP_OK);
    cJSON_DeleteItemFromObject(root,"output");CHECK(upload(root)==ESP_OK);
    CHECK(app_render_start()==ESP_OK);CHECK(app_effects_restore()==ESP_OK);
    app_render_get_output(&after);CHECK(fabsf(after.brightness_ceiling-0.3f)<0.001f);
    // The real boot path must bypass stored effects in BOTH kinds of safe boot.
    radio=EL_RADIO_SAFE;app_main();app_render_get_output(&after);
    CHECK(after.brightness_ceiling==0.05f);
    radio=EL_RADIO_ON;app_main();app_render_get_output(&after);
    CHECK(fabsf(after.brightness_ceiling-0.3f)<0.001f);
    reachable=false;confirmed=false;app_main();CHECK(confirmed);reachable=true;

    // Resting at a nonzero switch position must not restart an animation every frame.
    physical=2;render_frames(6);
    app_render_stats_t stats;app_render_stats(&stats);CHECK(stats.slot==2);CHECK(stats.effect_frame==6);
    audition=true;render_frames(6);app_render_stats(&stats);CHECK(stats.slot==3);
    audition=false;physical=4;render_frames(3);app_render_stats(&stats);CHECK(stats.slot==4);
    el_battery_config_t bc=EL_BATTERY_DEFAULT;bc.enabled=true;
    app_render_set_battery(&bc);pin_volts=5.5f/bc.divider_ratio;
    render_frames(2);app_render_stats(&stats);CHECK(stats.battery_scale==0);
    app_render_diagnostic(5);render_frames(2);app_render_stats(&stats);
    CHECK(stats.battery_scale==0 && stats.current_ma<=52.1f);
    cJSON_Delete(root);
    printf("%d firmware integration checks passed\n",checks);
    return 0;
}
