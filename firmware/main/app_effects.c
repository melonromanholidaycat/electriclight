#include "app_effects.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_render.h"
#include "cJSON.h"
#include "el_base64.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "effects";
static const char *NS = "electric";
static const char *KEY = "effects";

static char *s_stored;
static size_t s_stored_len;

// --- applying ----------------------------------------------------------------

static bool read_slot(const cJSON *slot, int index, app_slot_update_t *out,
                      char *err, size_t err_max)
{
    const cJSON *program = cJSON_GetObjectItem(slot, "program");
    if (!cJSON_IsString(program)) {
        snprintf(err, err_max, "slot %d has no program", index);
        return false;
    }

    const int n = el_base64_decode(program->valuestring, out->program, sizeof out->program);
    if (n <= 0) {
        snprintf(err, err_max, "slot %d: program is not valid base64", index);
        return false;
    }
    out->program_len = (size_t)n;

    const cJSON *name = cJSON_GetObjectItem(slot, "name");
    strlcpy(out->name, cJSON_IsString(name) ? name->valuestring : "unnamed", sizeof out->name);

    out->param_count = 0;
    const cJSON *params = cJSON_GetObjectItem(slot, "params");
    if (cJSON_IsArray(params)) {
        const int count = cJSON_GetArraySize(params);
        if (count > EL_MAX_PARAMS) {
            snprintf(err, err_max, "slot %d has %d parameters, the limit is %d",
                     index, count, EL_MAX_PARAMS);
            return false;
        }
        for (int i = 0; i < count; i++) {
            const cJSON *v = cJSON_GetArrayItem(params, i);
            out->params[i] = cJSON_IsNumber(v) ? (float)v->valuedouble : 0.0f;
        }
        out->param_count = (uint8_t)count;
    }
    return true;
}

static esp_err_t store(const char *json, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, KEY, json, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) return err;

    char *copy = malloc(len + 1);
    if (copy) {
        memcpy(copy, json, len);
        copy[len] = '\0';
        free(s_stored);
        s_stored = copy;
        s_stored_len = len;
    }
    return ESP_OK;
}

static esp_err_t apply(const char *json, size_t len, bool persist,
                       char *err_out, size_t err_max)
{
    if (err_max) err_out[0] = '\0';
    if (len == 0 || len > APP_EFFECTS_MAX_JSON) {
        snprintf(err_out, err_max, "payload is %u bytes; the limit is %d",
                 (unsigned)len, APP_EFFECTS_MAX_JSON);
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        snprintf(err_out, err_max, "not valid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *slots = cJSON_GetObjectItem(root, "slots");
    if (!cJSON_IsArray(slots)) {
        cJSON_Delete(root);
        snprintf(err_out, err_max, "no slots array");
        return ESP_ERR_INVALID_ARG;
    }

    // Decoded in full before anything is handed to the render loop. A document
    // with one bad slot in it must leave the guitar playing what it was playing,
    // not half of what was sent.
    const int count = cJSON_GetArraySize(slots);
    app_slot_update_t *updates = calloc((size_t)(count > 0 ? count : 1), sizeof *updates);
    if (!updates) {
        cJSON_Delete(root);
        snprintf(err_out, err_max, "out of memory");
        return ESP_ERR_NO_MEM;
    }

    int used = 0;
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        const cJSON *slot = cJSON_GetArrayItem(slots, i);
        if (cJSON_IsNull(slot)) continue; // leave this position alone
        updates[used].index = i;
        ok = read_slot(slot, i, &updates[used], err_out, err_max);
        if (ok) used++;
    }

    if (ok) ok = app_render_set_slots(updates, used, err_out, err_max);
    cJSON_Delete(root);
    free(updates);
    if (!ok) return ESP_ERR_INVALID_ARG;

    if (!persist) {
        // Restoring what is already in NVS. Writing it straight back would
        // spend a flash erase cycle on every boot storing bytes that are
        // already there.
        char *copy = malloc(len + 1);
        if (copy) {
            memcpy(copy, json, len);
            copy[len] = '\0';
            free(s_stored);
            s_stored = copy;
            s_stored_len = len;
        }
        ESP_LOGI(TAG, "%d slot(s) restored, %u bytes", used, (unsigned)len);
        return ESP_OK;
    }

    const esp_err_t err = store(json, len);
    if (err != ESP_OK) {
        // The guitar is already playing it; it just will not remember. Worth
        // saying so rather than reporting a success the next boot disproves.
        ESP_LOGE(TAG, "applied but not stored: %s", esp_err_to_name(err));
        snprintf(err_out, err_max, "playing, but could not be saved: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "%d slot(s) updated and stored, %u bytes", used, (unsigned)len);
    return ESP_OK;
}

const char *app_effects_stored(size_t *len_out)
{
    if (len_out) *len_out = s_stored_len;
    return s_stored;
}

esp_err_t app_effects_apply(const char *json, size_t len, char *err_out, size_t err_max)
{
    return apply(json, len, true, err_out, err_max);
}

esp_err_t app_effects_restore(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;

    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, KEY, NULL, &len);
    if (err != ESP_OK || len == 0 || len > APP_EFFECTS_MAX_JSON) {
        nvs_close(h);
        return ESP_ERR_NOT_FOUND;
    }

    char *json = malloc(len + 1);
    if (!json) { nvs_close(h); return ESP_ERR_NO_MEM; }
    err = nvs_get_blob(h, KEY, json, &len);
    nvs_close(h);
    if (err != ESP_OK) { free(json); return ESP_ERR_NOT_FOUND; }
    json[len] = '\0';

    char why[96];
    err = apply(json, len, false, why, sizeof why);
    free(json);
    if (err != ESP_OK) {
        // Stored effects that no longer load are worth shouting about: the
        // guitar has silently reverted to its built-ins, and the only way
        // anyone would otherwise notice is that it looks wrong on stage.
        ESP_LOGE(TAG, "stored effects will not load (%s); playing the built-ins", why);
    }
    return err;
}
