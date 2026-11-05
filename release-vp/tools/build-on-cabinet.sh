#!/bin/sh
# build-on-cabinet.sh — build the SDK and Release on the VectorPie cabinet
# itself (Debian trixie, arm64, with gcc, cmake, make and libsdl2-dev), and
# make the .vpk. Run on the cabinet from the folder holding sdk/ and
# release-vp/ (see RESUME.md: the sources are copied there with tar + scp).
set -e
HERE=$(pwd)
JOBS=$(nproc)

# sources from a Windows checkout may have CRLF line ends: the shell
# scripts and the manifest must not
sed -i 's/\r$//' sdk/tools/vp-pack/vp-pack.sh release-vp/package/game.ini

cmake -S sdk -B build-sdk -DCMAKE_BUILD_TYPE=Release \
      -DVP_BUILD_EXAMPLES=OFF -DVP_BUILD_TOOLS=OFF \
      -DCMAKE_INSTALL_PREFIX="$HERE/dist" > build-sdk.log
cmake --build build-sdk -j "$JOBS" >> build-sdk.log
cmake --install build-sdk >> build-sdk.log

cmake -S release-vp -B build-game -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$HERE/dist" > build-game.log
cmake --build build-game -j "$JOBS" 2>&1 | tee -a build-game.log | grep -E 'warning|error' || true
cmake --build build-game --target vpk 2>&1 | tee -a build-game.log | tail -5
ls -la build-game/*.vpk
