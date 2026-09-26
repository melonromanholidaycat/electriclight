#pragma once
#include "el_engine.h"
#include "el_bootstate.h"

// Board policy; the general-purpose engine remains usable for golden vectors.
#define EL_BOARD_MAX_MA 1500.0f
#define EL_BOARD_LED_MA 60.0f
bool el_output_validate(const el_output_t *o, char *why, size_t max);
bool el_restore_allowed(el_radio_mode_t mode);

typedef struct {
    bool enabled;
    float divider_ratio;
    float dim_volts;
    float cutoff_volts;
} el_battery_config_t;
extern const el_battery_config_t EL_BATTERY_DEFAULT;
bool el_battery_config_valid(const el_battery_config_t *cfg);
// Called with a multisampled voltage, once per second. The lowest scale is
// latched until reboot/reconfiguration so unloading the cells cannot oscillate.
float el_battery_scale(const el_battery_config_t *cfg, bool valid,
                       float volts, float previous);

// Direct diagnostic frames, already conservative linear RGB. No effect VM.
// 0 normal, 1 dark, 2 bass red, 3 treble green, 4 moving blue pixel,
// 5 static white. Frame 0's chase pixel is electrical index 0 (body end).
void el_diagnostic_frame(uint8_t *rgb, int per_strip, int mode, uint32_t frame);
