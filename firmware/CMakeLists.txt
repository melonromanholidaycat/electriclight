# electriclight firmware.
#
# The web UI is embedded from firmware/main/www/index.html.gz, which
# `node web/build.js` generates. It is the same bytes the simulator publishes to
# Pages, so the device and the simulator cannot be different builds of the page.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(electriclight)
