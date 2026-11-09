#!/bin/bash
# Builds "Breakout Mayhem.app" and packs it into a drag-to-Applications .dmg.
# Run from the folder that contains BreakoutMayhem/ (the build goes to ../build-mac, outside the project):
#   ./BreakoutMayhem/tools/make_dmg.sh
# Needs: brew install cmake sdl2 sdl2_ttf dylibbundler
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(dirname "$SRC")/build-mac"
VERSION=1.3.0
APP="$BUILD/Breakout Mayhem.app"

cmake -S "$SRC" -B "$BUILD" -DMACOS_BUNDLE=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build "$BUILD" -j

# copy the Homebrew SDL2 / SDL2_ttf / freetype dylibs into the bundle so it runs on other Macs
if command -v dylibbundler >/dev/null; then
    dylibbundler -od -b -x "$APP/Contents/MacOS/Breakout Mayhem" \
        -d "$APP/Contents/Frameworks" -p @executable_path/../Frameworks/
else
    echo "warning: dylibbundler not found (brew install dylibbundler) - the app will need Homebrew's SDL2"
fi

# Homebrew's "sdl2" is sdl2-compat: SDL2 implemented on top of SDL3, which it dlopen()s at runtime.
# dylibbundler can't see that dependency, so copy SDL3 next to the bundled libSDL2, where sdl2-compat
# looks first (@loader_path/libSDL3.dylib).
SDL3_LIB="$(brew --prefix sdl3 2>/dev/null)/lib/libSDL3.0.dylib"
if [ -f "$SDL3_LIB" ]; then
    mkdir -p "$APP/Contents/Frameworks"
    cp -L "$SDL3_LIB" "$APP/Contents/Frameworks/libSDL3.dylib"
    chmod u+w "$APP/Contents/Frameworks/libSDL3.dylib"
    install_name_tool -id @executable_path/../Frameworks/libSDL3.dylib "$APP/Contents/Frameworks/libSDL3.dylib"
    echo "bundled SDL3 for sdl2-compat"
fi
codesign --force --deep -s - "$APP"

STAGE="$BUILD/dmg"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
DMG="$BUILD/BreakoutMayhem-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "Breakout Mayhem" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Created $DMG"
