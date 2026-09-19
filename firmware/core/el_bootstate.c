#include "el_bootstate.h"

uint8_t el_bootstate_next(uint8_t stored)
{
    return stored >= 255 ? 255 : (uint8_t)(stored + 1);
}

uint8_t el_bootstate_on_healthy(void)
{
    return 0;
}

el_boot_mode_t el_bootstate_mode(uint8_t count, uint8_t threshold)
{
    if (threshold == 0) return EL_BOOT_NORMAL;
    return count >= threshold ? EL_BOOT_SAFE : EL_BOOT_NORMAL;
}

el_radio_mode_t el_radio_decide(el_boot_mode_t boot,
                                bool gesture,
                                bool brightness_low,
                                bool always_on)
{
    // Nothing overrides an automatic rescue. If the firmware got here by
    // failing to boot three times, the radio comes up on terms that do not
    // depend on any stored setting being correct.
    if (boot == EL_BOOT_SAFE) return EL_RADIO_SAFE;

    // The same gesture, with the brightness wound down, asks for safe mode
    // deliberately. One physical vocabulary rather than two.
    if (gesture && brightness_low) return EL_RADIO_SAFE;
    if (gesture) return EL_RADIO_ON;

    // Checked last, so it can never mask a deliberate request for safe mode.
    if (always_on) return EL_RADIO_ON;

    return EL_RADIO_OFF;
}

const char *el_radio_name(el_radio_mode_t mode)
{
    switch (mode) {
        case EL_RADIO_ON:   return "on";
        case EL_RADIO_SAFE: return "safe";
        default:            return "off";
    }
}
