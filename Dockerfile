FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_NDK_VERSION=26.1.10909125
ENV CMAKE_VERSION=3.22.1

# Install base dependencies and OpenJDK 17
RUN apt-get update && apt-get install -y \
    openjdk-17-jdk \
    curl \
    unzip \
    git \
    build-essential \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Install Android Command-line Tools
WORKDIR /opt
RUN mkdir -p ${ANDROID_SDK_ROOT}/cmdline-tools && \
    curl -o cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip && \
    unzip cmdline-tools.zip -d ${ANDROID_SDK_ROOT}/cmdline-tools && \
    mv ${ANDROID_SDK_ROOT}/cmdline-tools/cmdline-tools ${ANDROID_SDK_ROOT}/cmdline-tools/latest && \
    rm cmdline-tools.zip

ENV PATH=${PATH}:${ANDROID_SDK_ROOT}/cmdline-tools/latest/bin:${ANDROID_SDK_ROOT}/platform-tools

# Install Platform, NDK, and CMake
RUN yes | sdkmanager --licenses && \
    sdkmanager "platform-tools" \
               "platforms;android-34" \
               "build-tools;34.0.0" \
               "ndk;${ANDROID_NDK_VERSION}" \
               "cmake;${CMAKE_VERSION}"

WORKDIR /workspace
CMD ["/bin/bash"]
