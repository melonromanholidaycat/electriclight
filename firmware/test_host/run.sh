#!/bin/sh
# Builds and runs the firmware's host-testable code natively.
#
# Everything under components/core is plain C with no ESP-IDF in it, which is
# what makes this possible: the logic that decides whether a closed guitar stays
# reachable, and the evaluator that has to agree with the browser, both get
# checked in seconds instead of on hardware.
#
# Needs components/core/generated/el_vectors.h, so run `node web/build.js` first.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${TMPDIR:-/tmp}
core="$root/firmware/components/core"

if [ ! -f "$core/generated/el_vectors.h" ]; then
  echo "missing $core/generated/el_vectors.h - run 'node web/build.js' first" >&2
  exit 1
fi

# -Werror on purpose. This code gets flashed onto a guitar that has no serial
# console, so a warning here is the last cheap chance to hear about a problem.
CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -I$core"

for t in test_core test_eval test_safety; do
  # shellcheck disable=SC2086
  cc $CFLAGS -o "$out/el-$t" "$root/firmware/test_host/$t.c" "$core"/*.c -lm
  "$out/el-$t"
done

# Real JSON upload/restore and boot/render orchestration with fake ESP services.
# Debian/Ubuntu: apt-get install libcjson-dev
(cd "$root" && node --input-type=module -e "import {defaultLibrary} from './web/src/model/library.js'; import {buildEffectsPayload} from './web/src/ui/device.js'; console.log(JSON.stringify(buildEffectsPayload(defaultLibrary()).payload))") > "$out/el-payload.json"
cc $CFLAGS -D_POSIX_C_SOURCE=200809L -include "$root/firmware/test_host/stubs/compat.h" \
  -I"$root/firmware/test_host/stubs" -I"$root/firmware/main" \
  -I"${CJSON_INCLUDE:-/usr/include/cjson}" \
  "$root/firmware/test_host/test_integration.c" "$root/firmware/main/app_render.c" \
  "$root/firmware/main/app_effects.c" "$root/firmware/main/main.c" "$core"/*.c \
  -l:libcjson.so.1 -lm -o "$out/el-integration"
"$out/el-integration" "$out/el-payload.json"
cc $CFLAGS -include "$root/firmware/test_host/stubs/compat.h" \
  -I"$root/firmware/test_host/stubs" -I"$root/firmware/main" \
  "$root/firmware/test_host/test_wifi.c" "$root/firmware/main/app_wifi.c" \
  -o "$out/el-wifi"
"$out/el-wifi"
