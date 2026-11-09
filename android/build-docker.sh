#!/bin/bash
# Builds the Android APK in Docker. Run from anywhere:
#   ./BreakoutMayhem/android/build-docker.sh
# Output: build-android/BreakoutMayhem-<version>.apk next to the project folder.
set -e
SRC="$(cd "$(dirname "$0")/.." && pwd)"
PARENT="$(dirname "$SRC")"
NAME="$(basename "$SRC")"
IMAGE=breakoutmayhem-android

docker build --platform linux/amd64 -t "$IMAGE" "$SRC/android"
docker run --rm --platform linux/amd64 \
    -v "$PARENT":/work \
    -v breakoutmayhem-gradle-cache:/root/.gradle \
    "$IMAGE" bash "/work/$NAME/android/build.sh" "/work/$NAME" /work/build-android
