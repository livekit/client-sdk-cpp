# Copyright 2025 LiveKit
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
#/

FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
        libprotobuf-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y \
        build-essential \
        curl \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y \
        wget \
    && rm -rf /var/lib/apt/lists/*

RUN set -ex \
        && mkdir -p /tmp \
        && cd /tmp \
        && wget https://github.com/Kitware/CMake/releases/download/v4.1.5/cmake-4.1.5-linux-aarch64.sh \
        && chmod +x cmake-4.1.5-linux-aarch64.sh \
        && ./cmake-4.1.5-linux-aarch64.sh --skip-license --prefix=/usr/local \
        && rm cmake-4.1.5-linux-aarch64.sh \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y \
        ninja-build \
        cargo \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && apt-get install -y \
        git \
    && rm -rf /var/lib/apt/lists/*

RUN set -ex \
    && git clone https://github.com/protocolbuffers/protobuf.git \
    && cd protobuf \
    && git checkout v3.21.12 \
    && cmake -B build -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && ldconfig

RUN apt-get update && apt-get install -y \
        libmpfr-dev \
        libgmp3-dev \
        libmpc-dev \
    && rm -rf /var/lib/apt/lists/*

RUN set -ex \
        && mkdir -p /tmp \
        && cd /tmp \
        && wget https://ftp.gnu.org/gnu/gcc/gcc-14.1.0/gcc-14.1.0.tar.gz \
        && tar -xf gcc-14.1.0.tar.gz \
        && cd gcc-14.1.0 \
        && ./configure -v --build=$(uname -m)-linux-gnu --host=$(uname -m)-linux-gnu --target=$(uname -m)-linux-gnu --prefix=/usr/local/gcc-14.1.0 --enable-checking=release --enable-languages=c,c++ --disable-multilib --program-suffix=-14.1.0 \
        && make -j $(nproc) \
        && make install \
    && rm -rf /var/lib/apt/lists/*
