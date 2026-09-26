#include "el_safety.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool el_output_validate(const el_output_t *o, char *why, size_t max)
{
    const char *error = NULL;
    if (!isfinite(o->brightness_ceiling) || o->brightness_ceiling < 0 || o->brightness_ceiling > 1)
        error = "brightness ceiling must be 0 to 1";
    else if (!isfinite(o->gamma) || o->gamma < 1 || o->gamma > 4)
        error = "gamma must be 1 to 4";
    else if (!isfinite(o->ma_per_led) || o->ma_per_led < EL_BOARD_LED_MA || o->ma_per_led > 200)
        error = "LED current estimate must be 60 to 200 mA; lower values understate this tape's draw";
    else if (!isfinite(o->idle_current) || o->idle_current < 1 || o->idle_current > 10)
        error = "idle current must be 1 to 10 mA per LED";
    else if (!isfinite(o->current_budget) || o->current_budget < 50 || o->current_budget > EL_BOARD_MAX_MA)
        error = "current budget must be 50 to 1500 mA for this guitar";
    if (error && max) snprintf(why, max, "%s", error);
    return error == NULL;
}

bool el_restore_allowed(el_radio_mode_t mode) { return mode != EL_RADIO_SAFE; }

const el_battery_config_t EL_BATTERY_DEFAULT = {
    .enabled = false, .divider_ratio = 133.0f / 33.0f,
    .dim_volts = 6.6f, .cutoff_volts = 6.0f,
};

bool el_battery_config_valid(const el_battery_config_t *c)
{
    return isfinite(c->divider_ratio) && c->divider_ratio >= 3.5f && c->divider_ratio <= 6.0f &&
           isfinite(c->dim_volts) && isfinite(c->cutoff_volts) &&
           c->cutoff_volts >= 6.0f && c->dim_volts <= 8.0f &&
           c->dim_volts - c->cutoff_volts >= 0.2f;
}

float el_battery_scale(const el_battery_config_t *c, bool valid, float v, float previous)
{
    if (!c->enabled) return 1;
    if (!valid || !isfinite(v) || v < 0 || v > 10) return 0;
    float scale = (v - c->cutoff_volts) / (c->dim_volts - c->cutoff_volts);
    if (scale < 0) scale = 0;
    if (scale > 1) scale = 1;
    return scale < previous ? scale : previous;
}

void el_diagnostic_frame(uint8_t *rgb, int per_strip, int mode, uint32_t frame)
{
    memset(rgb, 0, (size_t)per_strip * 6);
    for (int side = 0; side < 2; side++) {
        for (int n = 0; n < per_strip; n++) {
            uint8_t *p = rgb + (side * per_strip + n) * 3;
            if (mode == 2 && side == 0) p[0] = 16;
            if (mode == 3 && side == 1) p[1] = 16;
            if (mode == 4 && n == (int)((frame / 15) % (uint32_t)per_strip)) p[2] = 16;
            if (mode == 5) p[0] = p[1] = p[2] = 16;
        }
    }
}
