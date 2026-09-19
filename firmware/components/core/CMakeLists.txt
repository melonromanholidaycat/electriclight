# Pure C, no ESP-IDF dependencies, so firmware/test_host can compile the same
# sources natively and CI can exercise them without a device.
idf_component_register(
    SRCS "el_gesture.c" "el_bootstate.c"
    INCLUDE_DIRS "."
)
