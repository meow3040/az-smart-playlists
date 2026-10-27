#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run on a Linux host, or in tools/Dockerfile.smart-tests with this repo at /repo and SDK at /sdk.
set -eu
cd "$(dirname "$0")/.."
SDK=${AZ_SDK:-/sdk}
CC=${CC:-gcc}
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
FLAGS="-O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -I$SDK/include"
$CC $FLAGS -o "$B/smart" tests/smart-playlists.c -lpthread -lm
"$B/smart"
$CC $FLAGS -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all -o "$B/smart-san" tests/smart-playlists.c -lpthread -lm
ASAN_OPTIONS=detect_leaks=0 "$B/smart-san"
$CC $FLAGS -fPIC -fvisibility=hidden -shared -o "$B/library-browser.so" library-browser/library-browser.c -lpthread -lm
$CC $FLAGS -fPIC -shared -o "$B/iocount.so" "$SDK/tools/harness/iocount.c" -ldl
$CC $FLAGS "-DAZ_HARNESS_SCENARIOS=\"$(pwd)/tests/scenarios.c\"" -rdynamic -o "$B/harness" "$SDK/tools/harness/harness.c" -ldl -lm -lpthread
LD_PRELOAD="$B/iocount.so" "$B/harness" "$B/library-browser.so" meow-smart-playlists 1 --functional
# A broken read path must fail the end-to-end scenario, so a green result has a control.
sed 's/if (result < 0)/if (result != 0)/' library-browser/library-browser.c > "$B/broken.c"
$CC $FLAGS -Ilibrary-browser -fPIC -fvisibility=hidden -shared -o "$B/broken.so" "$B/broken.c" -lpthread -lm
"$B/harness" "$B/broken.so" meow-smart-playlists 1 --functional > "$B/broken.log" 2>&1 || true
grep -q '^CHECK functional FAIL' "$B/broken.log"
printf '%s\n' 'Control: rejecting truncated results fails the functional scenario as expected.'
