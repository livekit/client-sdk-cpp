# Linux build environment for client-sdk-cpp.
# Ubuntu packages match the Linux jobs in .github/workflows/builds.yml,
# tests.yml, and make-release.yml. Pass --build-arg INSTALL_GSTREAMER=1 to also
# install the GStreamer packages from .github/actions/setup-capture.
# The source tree is bind-mounted at /work.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
# Empty means the default version in install-clang.sh.
ARG LLVM_VERSION

# ca-certificates, curl, git, git-lfs, xz-utils, sudo, and python3 are already
# present on GitHub-hosted runners. python3 is what the coverage job uses for
# gcovr. git is needed because FetchContent clones GoogleTest.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl git rsync xz-utils \
      python3 \
      build-essential cmake ninja-build pkg-config \
      llvm-dev libclang-dev clang \
      libva-dev libdrm-dev libgbm-dev libx11-dev libgl1-mesa-dev \
      libxext-dev libxcomposite-dev libxdamage-dev libxfixes-dev \
      libxrandr-dev libxi-dev libxkbcommon-dev \
      libasound2-dev libpulse-dev \
      libssl-dev \
      libprotobuf-dev protobuf-compiler \
      libabsl-dev \
      libcurl4-openssl-dev \
      libwayland-dev libdecor-0-dev \
      libunwind-dev libjpeg-dev \
      gdb jq \
    && rm -rf /var/lib/apt/lists/*

# webrtc-sys needs a recent clang for the hermetic libc++ in libwebrtc. Use the
# same script as CI. The build context is the repository root, and
# linux.Dockerfile.dockerignore limits it to this one file.
COPY client-sdk-rust/.github/scripts/install-clang.sh /tmp/install-clang.sh
RUN LLVM_ROOT=/opt/llvm /tmp/install-clang.sh \
    && rm /tmp/install-clang.sh

ENV CC=/opt/llvm/bin/clang \
    CXX=/opt/llvm/bin/clang++ \
    LIBCLANG_PATH=/opt/llvm/lib \
    RUSTUP_HOME=/opt/rustup \
    CARGO_HOME=/opt/cargo \
    PATH=/opt/cargo/bin:$PATH

RUN curl -fsSL https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain stable \
    && chmod -R a+rwX /opt/rustup /opt/cargo \
    && cargo --version

# Keep Rust outputs out of the synced source tree.
ENV CARGO_TARGET_DIR=/cargo-target

# Last install so a GStreamer toggle reuses the toolchain layers above.
# libunwind-dev is already in the base package list.
ARG INSTALL_GSTREAMER=0
RUN if [ "$INSTALL_GSTREAMER" = "1" ]; then \
      apt-get update && apt-get install -y --no-install-recommends \
        libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
      && rm -rf /var/lib/apt/lists/*; \
    fi

WORKDIR /work
CMD ["bash"]
