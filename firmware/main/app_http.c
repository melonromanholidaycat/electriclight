#include "app_http.h"

#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_effects.h"
#include "app_identity.h"
#include "app_log.h"
#include "app_mode.h"
#include "app_leds.h"
#include "app_ota.h"
#include "app_render.h"
#include "app_selftest.h"
#include "app_wifi.h"
#include "cJSON.h"
#include "el_engine.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http";

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

static esp_err_t get_index(httpd_req_t *req)
{
    const size_t len = index_html_gz_end - index_html_gz_start;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    // The page changes only when the firmware does, so let the phone cache it;
    // an OTA changes the URL's content and the version in /api/status says so.
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, len);
}

// The page fetches this at start-up and switches from simulator to live control
// when it sees device == "electriclight". Keep that field stable.
static esp_err_t get_status(httpd_req_t *req)
{
    const app_config_t *cfg = app_config_get();
    const esp_app_desc_t *desc = esp_app_get_description();
    char ip[16];
    app_wifi_ip_string(ip, sizeof(ip));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device", ELECTRICLIGHT_DEVICE_ID);
    cJSON_AddStringToObject(root, "name", cfg->name);
    cJSON_AddStringToObject(root, "version", ELECTRICLIGHT_VERSION);
    cJSON_AddStringToObject(root, "build", desc ? desc->version : "?");
    cJSON_AddStringToObject(root, "idf", desc ? desc->idf_ver : "?");
    cJSON_AddStringToObject(root, "wifi", app_wifi_mode_name());
    cJSON_AddStringToObject(root, "radio", el_radio_name(app_mode_current()));
    cJSON_AddBoolToObject(root, "safeMode", app_mode_boot() == EL_BOOT_SAFE);
    cJSON_AddNumberToObject(root, "bootCount", app_mode_boot_count());
    cJSON_AddBoolToObject(root, "radioAlwaysOn", cfg->radio_always_on);
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "slot", app_ota_running_slot());
    cJSON_AddBoolToObject(root, "pendingVerify", app_ota_pending_verify());
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap", (double)esp_get_free_heap_size());

    // Whether this firmware's evaluator still reproduces the golden vectors.
    // Cached from boot: re-running it costs a second or two, and /api/status
    // gets polled.
    // What a frame actually costs, which was an estimate until there was
    // hardware to ask.
    app_render_stats_t fr;
    app_render_stats(&fr);
    cJSON *render = cJSON_AddObjectToObject(root, "render");
    cJSON_AddNumberToObject(render, "frames", fr.frames);
    cJSON_AddNumberToObject(render, "late", fr.late);
    cJSON_AddNumberToObject(render, "lastUs", fr.last_us);
    cJSON_AddNumberToObject(render, "worstUs", fr.worst_us);
    cJSON_AddNumberToObject(render, "avgUs", fr.avg_us);
    cJSON_AddNumberToObject(render, "budgetUs", 1000000 / EL_FRAME_RATE);
    cJSON_AddNumberToObject(render, "currentMa", fr.current_ma);
    cJSON_AddBoolToObject(render, "limited", fr.limited);
    cJSON_AddNumberToObject(render, "slot", fr.slot);
    cJSON_AddStringToObject(render, "effect", fr.effect ? fr.effect : "none");
    if (fr.fault) cJSON_AddStringToObject(render, "fault", fr.fault);

    // The geometry the device is actually rendering, which the page cannot
    // change yet. Reported so a difference is visible rather than silent.
    el_output_t out;
    app_render_get_output(&out);
    cJSON *geo = cJSON_AddObjectToObject(root, "geometry");
    cJSON_AddNumberToObject(geo, "ledsPerStrip", EL_DEFAULT_GEOMETRY.leds_per_strip);
    cJSON_AddNumberToObject(geo, "frets", EL_DEFAULT_GEOMETRY.frets);
    cJSON_AddNumberToObject(geo, "scaleLength", EL_DEFAULT_GEOMETRY.scale_length);
    cJSON_AddNumberToObject(geo, "brightnessCeiling", out.brightness_ceiling);
    cJSON_AddNumberToObject(geo, "gamma", out.gamma);
    cJSON_AddNumberToObject(geo, "currentBudget", out.current_budget);

    const app_selftest_t *st = app_selftest_get();
    cJSON *self = cJSON_AddObjectToObject(root, "selftest");
    cJSON_AddBoolToObject(self, "ran", st->ran);
    cJSON_AddBoolToObject(self, "ok", st->ok);
    cJSON_AddNumberToObject(self, "ms", st->ms);
    cJSON_AddStringToObject(self, "summary", st->summary);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t get_log(httpd_req_t *req)
{
    // Sized to hold the whole ring buffer; this is the only window into a
    // device with no serial port, so truncating it would defeat the point.
    char *buf = malloc(4224);
    if (!buf) return httpd_resp_send_500(req);
    size_t len = app_log_read(buf, 4224);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, buf, len);
    free(buf);
    return err;
}

// Raw firmware image in the body. Deliberately not multipart: iOS Safari can
// PUT a file from the Files app, and a parser is one more thing to get wrong on
// the path that exists to rescue a broken device.
static esp_err_t post_ota(httpd_req_t *req)
{
    app_ota_session_t *session = NULL;
    esp_err_t err = app_ota_begin(&session);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    char *chunk = malloc(4096);
    if (!chunk) {
        app_ota_abort(session);
        return httpd_resp_send_500(req);
    }

    int remaining = req->content_len;
    while (remaining > 0) {
        int got = httpd_req_recv(req, chunk, remaining < 4096 ? remaining : 4096);
        if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (got <= 0) {
            free(chunk);
            app_ota_abort(session);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload interrupted");
            return ESP_FAIL;
        }
        err = app_ota_write(session, chunk, got);
        if (err != ESP_OK) {
            free(chunk);
            app_ota_abort(session);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
            return ESP_FAIL;
        }
        remaining -= got;
    }
    free(chunk);

    err = app_ota_finish(session);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");

    ESP_LOGW(TAG, "restarting into the new image");
    // Otherwise the neck holds its last frame through the reboot, which looks
    // like the update hung at exactly the moment nobody wants to see that.
    app_leds_blank();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t max)
{
    if (req->content_len >= max) return ESP_ERR_INVALID_SIZE;
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        got += n;
    }
    buf[got] = '\0';
    return ESP_OK;
}

static esp_err_t post_wifi(httpd_req_t *req)
{
    char body[256];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    const cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected {ssid, password}");
        return ESP_FAIL;
    }

    esp_err_t err = app_config_set_wifi(ssid->valuestring,
                                        cJSON_IsString(pass) ? pass->valuestring : "");
    cJSON_Delete(root);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// Turning this off is what arms the gesture. It is a separate endpoint rather
// than part of a general settings write because it is the one setting that can
// make the guitar unreachable, and it should be hard to change by accident.
static esp_err_t post_radio_policy(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    const cJSON *always = cJSON_GetObjectItem(root, "alwaysOn");
    if (!cJSON_IsBool(always)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected {alwaysOn: true|false}");
        return ESP_FAIL;
    }
    const bool on = cJSON_IsTrue(always);
    cJSON_Delete(root);

    esp_err_t err = app_config_set_radio_always_on(on);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "radio always-on is now %s; takes effect at the next restart",
             on ? "enabled" : "disabled");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, on ? "{\"ok\":true,\"alwaysOn\":true}"
                                      : "{\"ok\":true,\"alwaysOn\":false}");
}

// Re-runs the golden vectors now. Seconds, not milliseconds: every frame of
// every case has to be rendered, because `prev` makes an effect a state machine
// and a skipped frame is a different answer, and the S3 emulates the doubles.
//
// The server serves nothing else while this runs, and once step 5 is driving
// LEDs it will cost frames too. It is a thing a person asks for once after an
// update, never something the page should poll.
static esp_err_t get_selftest(httpd_req_t *req)
{
    const app_selftest_t *st = app_selftest_run();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", st->ok);
    cJSON_AddNumberToObject(root, "ms", st->ms);
    cJSON_AddStringToObject(root, "summary", st->summary);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return httpd_resp_send_500(req);

    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

// The effects the guitar is playing. GET returns exactly what was last
// accepted, so a replacement phone can recover the library from the instrument
// rather than the other way round.
static esp_err_t get_effects(httpd_req_t *req)
{
    size_t len = 0;
    const char *stored = app_effects_stored(&len);
    httpd_resp_set_type(req, "application/json");
    if (!stored) {
        // Not an error. It means nobody has sent any, so the built-ins are
        // playing - and the page needs to be able to tell those apart.
        return httpd_resp_sendstr(req, "{\"stored\":false}");
    }
    return httpd_resp_send(req, stored, (ssize_t)len);
}

static esp_err_t post_effects(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > APP_EFFECTS_MAX_JSON) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "payload too large");
        return ESP_FAIL;
    }

    char *body = malloc((size_t)req->content_len + 1);
    if (!body) return httpd_resp_send_500(req);

    int got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, body + got, (size_t)(req->content_len - got));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "upload interrupted");
            return ESP_FAIL;
        }
        got += n;
    }
    body[got] = '\0';

    char why[128];
    const esp_err_t err = app_effects_apply(body, (size_t)got, why, sizeof why);
    free(body);

    if (err != ESP_OK) {
        // The reason matters more than the status here: it is the only thing
        // standing between the owner and guessing why an effect would not load.
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "error", why);
        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json) return httpd_resp_send_500(req);
        esp_err_t sent = httpd_resp_sendstr(req, json);
        free(json);
        return sent;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// Auditioning an effect from the phone without touching the five-way.
static esp_err_t post_slot(httpd_req_t *req)
{
    char buf[64];
    const int len = req->content_len < (int)sizeof buf - 1 ? req->content_len : (int)sizeof buf - 1;
    int got = 0;
    while (got < len) {
        const int n = httpd_req_recv(req, buf + got, (size_t)(len - got));
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "interrupted");
        got += n;
    }
    buf[got] = '\0';

    cJSON *root = cJSON_Parse(buf);
    const cJSON *slot = cJSON_GetObjectItem(root, "slot");
    if (!cJSON_IsNumber(slot) || slot->valueint < 0 ||
        slot->valueint >= app_render_slot_count()) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "no such slot");
        return ESP_FAIL;
    }
    const int index = slot->valueint;
    cJSON_Delete(root);

    app_render_select(index);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static const httpd_uri_t ROUTES[] = {
    { .uri = "/",            .method = HTTP_GET,  .handler = get_index },
    { .uri = "/index.html",  .method = HTTP_GET,  .handler = get_index },
    { .uri = "/api/status",  .method = HTTP_GET,  .handler = get_status },
    { .uri = "/api/log",     .method = HTTP_GET,  .handler = get_log },
    { .uri = "/api/selftest",.method = HTTP_GET,  .handler = get_selftest },
    { .uri = "/api/ota",     .method = HTTP_POST, .handler = post_ota },
    { .uri = "/api/wifi",    .method = HTTP_POST, .handler = post_wifi },
    { .uri = "/api/radio",   .method = HTTP_POST, .handler = post_radio_policy },
    { .uri = "/api/effects", .method = HTTP_GET,  .handler = get_effects },
    { .uri = "/api/effects", .method = HTTP_POST, .handler = post_effects },
    { .uri = "/api/slot",    .method = HTTP_POST, .handler = post_slot },
};

esp_err_t app_http_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(ROUTES) / sizeof(ROUTES[0]) + 2;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    // A firmware image takes a while to arrive over a phone hotspot.
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 30;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        httpd_register_uri_handler(server, &ROUTES[i]);
    }
    ESP_LOGI(TAG, "serving on port %d", config.server_port);
    return ESP_OK;
}
