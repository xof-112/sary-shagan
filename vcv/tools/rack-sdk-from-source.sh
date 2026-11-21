#!/usr/bin/env bash
# Baut ein header-only "Rack SDK" aus dem Rack-Quelltext, für den Windows-Cross-Build
# unter Linux, wenn das offizielle SDK (vcvrack.com/downloads) nicht erreichbar ist.
# Auf Windows/macOS lieber das offizielle Rack-SDK-2.x nehmen (siehe vcv/README.md).
#
#   vcv/tools/rack-sdk-from-source.sh <Zielordner>      z. B. ../Rack-SDK
#
# Danach (aus vcv/):
#   W="CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ STRIP=x86_64-w64-mingw32-strip"
#   make RACK_DIR=<Zielordner> $W build/src/SaryShagan.cpp.o build/src/plugin.cpp.o \
#        build/ext/nt_host.cpp.o build/ext/nt_api.cpp.o build/ext/nt_globals.cpp.o build/ext/sary_shagan.cpp.o
#   python3 tools/mklibrack.py <Zielordner> build/src/*.o build/ext/*.o   # Import-Lib für libRack.dll
#   make RACK_DIR=<Zielordner> $W && make RACK_DIR=<Zielordner> $W dist   # dist: zstd-Fehler am Ende ist egal
#
# Braucht: git, Debian/Ubuntu-Pakete libjansson-dev libglew-dev libspeexdsp-dev, mingw-w64.
set -euo pipefail
DEST=${1:?Zielordner angeben}
RACK_TAG=${RACK_TAG:-v2.6.4}

git clone --depth 1 --branch "$RACK_TAG" https://github.com/VCVRack/Rack.git "$DEST"
cd "$DEST"
git submodule update --init --depth 1 dep/nanovg dep/nanosvg dep/oui-blendish dep/simde dep/glfw
mkdir -p dep/include
cp dep/nanovg/src/nanovg.h dep/nanovg/src/nanovg_gl.h dep/nanovg/src/nanovg_gl_utils.h \
   dep/nanosvg/src/nanosvg.h dep/nanosvg/src/nanosvgrast.h dep/oui-blendish/blendish.h dep/include/
cp -r dep/glfw/include/GLFW dep/simde/simde dep/include/
cp -r /usr/include/GL /usr/include/speex dep/include/
cp /usr/include/jansson.h /usr/include/jansson_config.h dep/include/
# pffft: das Rack-Submodul liegt auf bitbucket; der Header ist derselbe wie im GitHub-Spiegel
git clone --depth 1 https://github.com/marton78/pffft.git /tmp/pffft-$$
cp /tmp/pffft-$$/include/pffft/pffft.h dep/include/ && rm -rf /tmp/pffft-$$
echo "Rack-Header in $DEST bereit (Rack $RACK_TAG)."
