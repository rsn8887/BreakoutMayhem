# Breakout Mayhem - Android build environment
# Android SDK + NDK + Gradle + the SDL2 / SDL2_ttf sources, pinned to fixed versions.
# Google ships the NDK for x86-64 Linux only, so the image is linux/amd64 (on Apple Silicon it runs under emulation).
FROM --platform=linux/amd64 eclipse-temurin:17-jdk-jammy

ENV ANDROID_SDK_ROOT=/opt/android-sdk \
    ANDROID_HOME=/opt/android-sdk

RUN apt-get update && apt-get install -y --no-install-recommends git unzip wget ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Android command line tools, SDK platform, build tools, NDK and CMake
ARG CMDLINE_TOOLS=11076708
RUN mkdir -p $ANDROID_SDK_ROOT/cmdline-tools && cd /tmp && \
    wget -q https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip && \
    unzip -q commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip -d $ANDROID_SDK_ROOT/cmdline-tools && \
    mv $ANDROID_SDK_ROOT/cmdline-tools/cmdline-tools $ANDROID_SDK_ROOT/cmdline-tools/latest && \
    rm commandlinetools-linux-${CMDLINE_TOOLS}_latest.zip
ENV PATH=$PATH:$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$ANDROID_SDK_ROOT/platform-tools:/opt/gradle/bin
RUN yes | sdkmanager --licenses > /dev/null && \
    sdkmanager "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" "cmake;3.22.1"

# Gradle (same version as SDL's own Android project template)
ARG GRADLE_VERSION=8.1.1
RUN cd /tmp && wget -q https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip && \
    unzip -q gradle-${GRADLE_VERSION}-bin.zip -d /opt && mv /opt/gradle-${GRADLE_VERSION} /opt/gradle && \
    rm gradle-${GRADLE_VERSION}-bin.zip

# SDL2 and SDL2_ttf sources (SDL_ttf with its bundled FreeType)
RUN git clone --depth 1 -b release-2.32.8 https://github.com/libsdl-org/SDL.git /opt/SDL2 && \
    git clone --depth 1 -b release-2.24.0 https://github.com/libsdl-org/SDL_ttf.git /opt/SDL2_ttf && \
    cd /opt/SDL2_ttf && git submodule update --init --depth 1 external/freetype

WORKDIR /work
