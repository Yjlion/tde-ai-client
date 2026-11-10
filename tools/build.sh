#!/bin/bash
# Builds and installs tde-ai-client inside the tools/ Docker image:
#   docker build -t tde-ai-client-build tools/
#   docker run --rm -v "$PWD":/src tde-ai-client-build /src/tools/build.sh
set -e
mkdir -p /tmp/build && cd /tmp/build
cmake "${SRC:-/src}" -DCMAKE_INSTALL_PREFIX=/opt/trinity -DCMAKE_CXX_FLAGS=-Wall -DBUILD_DOC=${BUILD_DOC:-ON} >/dev/null
make -j"$(nproc)" 2>&1 | grep -E "error|warning|Error" -A6 || true
make install >/dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
echo "build ok"
