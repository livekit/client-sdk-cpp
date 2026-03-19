# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Build with:
# docker build -t livekit-cpp-sdk . -f docker/Dockerfile
# From repo root.
# Run with:
# docker run -it --network host livekit-cpp-sdk:latest bash

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV HOME=/root

# Install make, pkg-config, and base build deps (pkg-config + libglib2.0-dev for Rust glib-sys, libasound2-dev for alsa-sys)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    libasound2-dev \
    libclang-dev \
    libdrm-dev \
    libglib2.0-dev \
    libspdlog-dev \
    libssl-dev \
    libunwind-dev \
    libusb-1.0-0-dev \
    libva-dev \
    make \
    ninja-build \
    pkg-config \
    wget \
    xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Install GCC 14 first (slow; keep early for better layer caching)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    flex \
    libgmp-dev \
    libmpc-dev \
    libmpfr-dev \
    && rm -rf /var/lib/apt/lists/*

RUN cd $HOME \
    && wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz \
    && tar xf gcc-14.2.0.tar.xz \
    && cd gcc-14.2.0 \
    && ./configure --prefix=$HOME/gcc-14 --enable-languages=c,c++ --disable-multilib --disable-bootstrap \
    && make -j$(nproc) \
    && make install \
    && cd $HOME \
    && rm -rf gcc-14.2.0 gcc-14.2.0.tar.xz

RUN mkdir -p /home/installs && cp -a $HOME/gcc-14/bin/* /home/installs/

ENV CC=$HOME/gcc-14/bin/gcc
ENV CXX=$HOME/gcc-14/bin/g++
ENV LD_LIBRARY_PATH=$HOME/gcc-14/lib64

# Install CMake 3.31.5 for current architecture (prefix dir must exist before installer runs)
# TARGETARCH is set by Docker BuildKit when building (amd64 -> x86_64, arm64 -> aarch64)
ARG TARGETARCH
RUN mkdir -p $HOME/cmake-3.31 \
    && CMAKE_ARCH=$(case "$TARGETARCH" in amd64) echo x86_64 ;; arm64) echo aarch64 ;; *) echo "$TARGETARCH" ;; esac) \
    && wget -q "https://github.com/Kitware/CMake/releases/download/v3.31.5/cmake-3.31.5-linux-${CMAKE_ARCH}.sh" \
    -O cmake-installer.sh \
    && chmod +x cmake-installer.sh \
    && ./cmake-installer.sh --skip-license --prefix=$HOME/cmake-3.31 \
    && rm cmake-installer.sh

ENV PATH=$HOME/cmake-3.31/bin:$PATH

# Install Rust (non-interactive)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y \
    && . $HOME/.cargo/env && rustup default stable

ENV PATH=$HOME/.cargo/bin:$PATH

# Deps for SDL (X11 + OpenGL/GLX for FindOpenGL)
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgl1-mesa-dev \
        libx11-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxrandr-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy project source (build context = repo root)
WORKDIR /client-sdk-cpp
RUN mkdir -p /client-sdk-cpp
COPY src /client-sdk-cpp/src
COPY include /client-sdk-cpp/include
COPY bridge /client-sdk-cpp/bridge
COPY build.sh /client-sdk-cpp/build.sh
COPY CMakePresets.json /client-sdk-cpp/CMakePresets.json
COPY build.cmd /client-sdk-cpp/build.cmd
COPY client-sdk-rust /client-sdk-cpp/client-sdk-rust
COPY data /client-sdk-cpp/data
COPY examples /client-sdk-cpp/examples
COPY cmake /client-sdk-cpp/cmake
COPY CMakeLists.txt /client-sdk-cpp/CMakeLists.txt
COPY build.h.in /client-sdk-cpp/build.h.in
COPY build.sh /client-sdk-cpp/build.sh
COPY build.cmd /client-sdk-cpp/build.cmd

# Configure Rust linker: use full GCC path so liblto_plugin.so is found (not /home/installs/ which has no plugin)
RUN mkdir -p /client-sdk-cpp/client-sdk-rust/.cargo \
    && printf '%s\n' '[target.x86_64-unknown-linux-gnu]' 'linker = "/root/gcc-14/bin/g++"' \
        '[target.aarch64-unknown-linux-gnu]' 'linker = "/root/gcc-14/bin/g++"' > /client-sdk-cpp/client-sdk-rust/.cargo/config.toml

