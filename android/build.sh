#!/bin/bash
# Breakout Mayhem - Android build (runs inside the Docker image, see build-docker.sh)
#   build.sh <project dir> <output dir>
# The Gradle project is assembled in <output dir>/gradle-project, so the project folder itself stays clean.
set -e
SRC="$(cd "${1:?project dir}" && pwd)"
OUT="${2:?output dir}"
SDL2_SRC="${SDL2_SRC:-/opt/SDL2}"
SDL2_TTF_SRC="${SDL2_TTF_SRC:-/opt/SDL2_ttf}"
VERSION="$(sed -n 's/^project(BreakoutMayhem VERSION \([0-9.]*\).*/\1/p' "$SRC/CMakeLists.txt")"

GP="$OUT/gradle-project"
mkdir -p "$OUT"
rm -rf "$GP/app/src" "$GP/app/assets"
mkdir -p "$GP"
cp -R "$SRC/android/project/." "$GP/"

# SDL's Java glue must match the SDL version that is compiled in
mkdir -p "$GP/app/src/main/java/org/libsdl/app"
cp "$SDL2_SRC"/android-project/app/src/main/java/org/libsdl/app/*.java "$GP/app/src/main/java/org/libsdl/app/"

# game data goes into the APK assets (SDL reads relative paths from there)
mkdir -p "$GP/app/assets/data"
cp "$SRC/data/Poppins-Bold.ttf" "$SRC/data/Poppins-OFL.txt" "$GP/app/assets/data/"

cd "$GP"
gradle --no-daemon assembleRelease \
    -PSDL2_SRC="$SDL2_SRC" -PSDL2_TTF_SRC="$SDL2_TTF_SRC" -PGAME_SRC="$SRC/src" -PVERSION_NAME="$VERSION"

cp app/build/outputs/apk/release/app-release.apk "$OUT/BreakoutMayhem-$VERSION.apk"
echo "Created $OUT/BreakoutMayhem-$VERSION.apk"
