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

for t in test_core test_eval; do
  # shellcheck disable=SC2086
  cc $CFLAGS -o "$out/el-$t" "$root/firmware/test_host/$t.c" "$core"/*.c -lm
  "$out/el-$t"
done
