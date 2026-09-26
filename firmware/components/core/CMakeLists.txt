# Pure C, no ESP-IDF dependencies, so firmware/test_host can compile the same
# sources natively and CI can exercise them without a device. That is not a
# tidiness rule: the effect evaluator's whole job is to agree with a copy of
# itself that runs in a browser, and the only way to check that cheaply is to
# build it for the host.
#
# generated/ holds el_vectors.h, written by web/build.js from
# web/test/vectors.json. Generated, not committed - same rule as the embedded
# page, and for the same reason: one source of truth, compiled into two places.
idf_component_register(
    SRCS "el_gesture.c" "el_bootstate.c" "el_safety.c"
         "el_eval.c" "el_program.c" "el_base64.c" "el_geometry.c" "el_engine.c" "el_selftest.c"
    INCLUDE_DIRS "."
)

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/generated/el_vectors.h")
    message(FATAL_ERROR
        "Missing components/core/generated/el_vectors.h. Run `node web/build.js` first.")
endif()
