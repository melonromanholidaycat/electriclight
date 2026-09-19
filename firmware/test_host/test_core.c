// Host tests for firmware/components/core. Plain C, no framework, no device:
//   gcc -o /tmp/t firmware/test_host/test_core.c firmware/components/core/*.c -Ifirmware/components/core && /tmp/t
//
// These cover the decisions that determine whether a closed guitar can be
// reached at all, which is exactly the class of bug that is most expensive to
// find on hardware and cheapest to find here.

#include <stdio.h>
#include <string.h>

#include "el_bootstate.h"
#include "el_gesture.h"

static int failures = 0;
static int checks = 0;

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  x %s\n", what);
    }
}

// --- gesture -----------------------------------------------------------------

// Feeds a sweep across the given positions, one sample every `step_ms`.
static void sweep(el_gesture_t *g, uint32_t *now, const int *path, int n, uint32_t step_ms)
{
    for (int i = 0; i < n; i++) {
        *now += step_ms;
        el_gesture_update(g, *now, path[i]);
    }
}

static void test_gesture(void)
{
    el_gesture_t g;
    uint32_t now = 1000;

    // A full sweep in either direction is the gesture.
    const int up[] = {0, 1, 2, 3, 4};
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, up, 5, 50);
    check(el_gesture_detected(&g), "sweep first to last is detected");

    const int down[] = {4, 3, 2, 1, 0};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, down, 5, 50);
    check(el_gesture_detected(&g), "sweep last to first is detected");

    // Starting anywhere, as long as both ends get visited.
    const int middle_out[] = {2, 1, 0, 1, 2, 3, 4};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, middle_out, 7, 50);
    check(el_gesture_detected(&g), "starting mid-travel still works");

    // Anything short of both ends is not the gesture.
    const int one_notch[] = {2, 3};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, one_notch, 2, 50);
    check(!el_gesture_detected(&g), "a single notch is not the gesture");

    const int nearly[] = {1, 2, 3, 4};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, nearly, 4, 50);
    check(!el_gesture_detected(&g), "reaching only one end is not the gesture");

    // Sitting still at an end position for the whole window is not a sweep.
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    for (int i = 0; i < 50; i++) { now += 50; el_gesture_update(&g, now, 0); }
    check(!el_gesture_detected(&g), "resting at an end position is not the gesture");

    // In-transit samples must not break the sweep apart.
    const int with_transit[] = {0, -1, 1, -1, 2, -1, 3, -1, 4};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, with_transit, 9, 20);
    check(el_gesture_detected(&g), "break-before-make gaps do not interrupt a sweep");

    // A sweep made entirely of transit and the two ends still counts: a fast
    // turn may never be sampled at an intermediate detent.
    const int fast[] = {0, -1, -1, 4};
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, fast, 4, 30);
    check(el_gesture_detected(&g), "a fast sweep missing the middle still counts");

    // The window closes.
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    now += 6000;
    el_gesture_update(&g, now, 0);
    el_gesture_update(&g, now + 50, 4);
    check(!el_gesture_detected(&g), "a sweep after the window is ignored");
    check(!el_gesture_window_open(&g, now), "the window reports itself closed");

    // Half in, half out: the window closing mid-gesture must not complete it.
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    el_gesture_update(&g, now + 100, 0);
    el_gesture_update(&g, now + 9000, 4);
    check(!el_gesture_detected(&g), "a sweep straddling the window edge is ignored");

    // Once seen, it stays seen.
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    sweep(&g, &now, up, 5, 50);
    el_gesture_update(&g, now + 10000, 2);
    check(el_gesture_detected(&g), "detection is not undone by later samples");

    // Nonsense positions are ignored rather than trusted.
    now = 1000;
    el_gesture_begin(&g, now, 5000);
    el_gesture_update(&g, now + 10, 99);
    el_gesture_update(&g, now + 20, -7);
    check(!el_gesture_detected(&g), "out-of-range positions are ignored");

    // The millisecond counter wraps after 49 days. The window must not.
    now = 0xFFFFFF00u;
    el_gesture_begin(&g, now, 5000);
    check(el_gesture_window_open(&g, now + 1000), "the window survives a counter wrap");
    el_gesture_update(&g, now + 100, 0);
    el_gesture_update(&g, now + 200, 4);   // wraps past zero
    check(el_gesture_detected(&g), "a sweep across a counter wrap is detected");
}

// --- boot state --------------------------------------------------------------

static void test_bootstate(void)
{
    check(el_bootstate_next(0) == 1, "the first boot counts");
    check(el_bootstate_next(255) == 255, "the count saturates rather than wrapping");
    check(el_bootstate_on_healthy() == 0, "reaching healthy clears the count");

    check(el_bootstate_mode(1, EL_BOOTLOOP_THRESHOLD) == EL_BOOT_NORMAL, "one bad boot is normal");
    check(el_bootstate_mode(2, EL_BOOTLOOP_THRESHOLD) == EL_BOOT_NORMAL, "two bad boots is normal");
    check(el_bootstate_mode(3, EL_BOOTLOOP_THRESHOLD) == EL_BOOT_SAFE, "three bad boots is safe mode");
    check(el_bootstate_mode(200, EL_BOOTLOOP_THRESHOLD) == EL_BOOT_SAFE, "and it stays safe mode");
    check(el_bootstate_mode(200, 0) == EL_BOOT_NORMAL, "a zero threshold disables the rescue");

    // The whole point: a guitar that cannot boot must end up reachable without
    // anyone touching it.
    uint8_t count = 0;
    el_boot_mode_t mode = EL_BOOT_NORMAL;
    for (int boot = 1; boot <= 3; boot++) {
        count = el_bootstate_next(count);
        mode = el_bootstate_mode(count, EL_BOOTLOOP_THRESHOLD);
    }
    check(mode == EL_BOOT_SAFE, "three failed boots in a row reach safe mode unaided");

    // And a rescue that worked must not be sticky.
    count = el_bootstate_on_healthy();
    count = el_bootstate_next(count);
    check(el_bootstate_mode(count, EL_BOOTLOOP_THRESHOLD) == EL_BOOT_NORMAL,
          "the boot after a healthy one is ordinary again");
}

// --- radio policy ------------------------------------------------------------

static void test_radio(void)
{
    check(el_radio_decide(EL_BOOT_NORMAL, false, false, false) == EL_RADIO_OFF,
          "off is the default");
    check(el_radio_decide(EL_BOOT_NORMAL, true, false, false) == EL_RADIO_ON,
          "the gesture brings the radio up");
    check(el_radio_decide(EL_BOOT_NORMAL, true, true, false) == EL_RADIO_SAFE,
          "the gesture with the brightness down asks for safe mode");
    check(el_radio_decide(EL_BOOT_NORMAL, false, true, false) == EL_RADIO_OFF,
          "brightness alone does nothing - the knob gets left there");

    check(el_radio_decide(EL_BOOT_SAFE, false, false, false) == EL_RADIO_SAFE,
          "a boot-loop rescue needs no gesture");
    check(el_radio_decide(EL_BOOT_SAFE, true, false, true) == EL_RADIO_SAFE,
          "nothing downgrades a boot-loop rescue");

    check(el_radio_decide(EL_BOOT_NORMAL, false, false, true) == EL_RADIO_ON,
          "always-on covers a board with no controls wired");
    check(el_radio_decide(EL_BOOT_NORMAL, true, true, true) == EL_RADIO_SAFE,
          "always-on does not mask a deliberate request for safe mode");

    check(strcmp(el_radio_name(EL_RADIO_OFF), "off") == 0, "names: off");
    check(strcmp(el_radio_name(EL_RADIO_ON), "on") == 0, "names: on");
    check(strcmp(el_radio_name(EL_RADIO_SAFE), "safe") == 0, "names: safe");
}

int main(void)
{
    test_gesture();
    test_bootstate();
    test_radio();

    if (failures) {
        printf("\n%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
