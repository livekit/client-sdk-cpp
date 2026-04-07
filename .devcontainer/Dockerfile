# Use Ubuntu 24.04-based image for a very recent CMake (3.28+)
FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Install system dependencies via apt
RUN apt update && apt install -y \
    build-essential cmake ninja-build pkg-config \
    llvm-dev libclang-dev clang \
    libprotobuf-dev protobuf-compiler libabsl-dev \
    libssl-dev libva-dev libdrm-dev libgbm-dev libx11-dev libgl1-mesa-dev

RUN apt update -y && apt install -y \
    libasound2-dev \
    libssl-dev \
    libx11-dev \
    libgl1-mesa-dev \
    libxext-dev \
    libdrm-dev \
    libgbm-dev \
    libxfixes-dev \
    libxdamage-dev \
    libxrandr-dev \
    libxcomposite-dev \
    libglib2.0-dev

# Install Rust for the 'vscode' user
USER vscode
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/home/vscode/.cargo/bin:${PATH}"

# Default back to root for any post-install logic, though 'vscode' is the dev user
USER root