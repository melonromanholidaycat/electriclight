# electriclight firmware.
#
# The web UI is embedded from firmware/main/www/index.html.gz, which
# `node web/build.js` generates. It is the same bytes the simulator publishes to
# Pages, so the device and the simulator cannot be different builds of the page.

cmake_minimum_required(VERSION 3.16)

# firmware/components/core holds the decision logic as plain C with no ESP-IDF
# in it, so firmware/test_host can compile the same sources natively. ESP-IDF
# finds components/ on its own; it does not search anywhere else.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(electriclight)
