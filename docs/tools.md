# Developer tools

This SDK uses several tools and checks to enforce code quality. All of these
are also enforced in CI on PRs.

## Clang tools

The shared configuration and tooling scripts come from the
[`livekit/cpp-tools`](https://github.com/livekit/cpp-tools) submodule.

- **`clang-tidy`** — static analysis. See
  [cpp-tools/.clang-tidy](https://github.com/livekit/cpp-tools/blob/main/.clang-tidy) for the
  enabled checks. Enforced in CI on PR.
- **`clang-format`** — code formatting and style consistency. See
  [cpp-tools/.clang-format](https://github.com/livekit/cpp-tools/blob/main/.clang-format) for the rules. Enforced in CI on PR.

> **Note (Windows):** `clang-tidy` is not currently driven by our scripts on
> Windows. The MSBuild CMake generator doesn't emit
> `compile_commands.json`, which `clang-tidy` requires. The Ninja generator
> does, so manual invocation is possible. `clang-format` similarly needs to be
> installed and run manually on Windows, pointing at the root `.clang-format`.

### Install

**macOS:**

```bash
brew install llvm
```

This installs `clang-format`, `clang-tidy`, and `run-clang-tidy`. Homebrew may
ask you to add `/opt/homebrew/opt/llvm/bin` (Apple Silicon) or
`/usr/local/opt/llvm/bin` (Intel) to your `PATH`.

**Linux (Ubuntu/Debian):**

```bash
sudo apt-get install clang-format clang-tidy clang-tools
```

Install the shared configuration symlinks from the repository root:

```bash
./cpp-tools/install.sh
```

### Run `clang-tidy`

1. Generate `compile_commands.json` and the protobuf headers via a release build:

   ```bash
   ./build.sh release
   ```

2. Run the wrapper, which uses the same file set, regex filters, and
   `.clang-tidy` config as CI:

   ```bash
   ./scripts/clang-tidy.sh
   ```

With no arguments, runs against every relevant file in the repository against
the checks in `.clang-tidy`.

```bash
./scripts/clang-tidy.sh src/ffi_client.cpp # Check just this file
./scripts/clang-tidy.sh -j 4               # Override worker count
./scripts/clang-tidy.sh --fix              # Apply fixes
```

Output is captured to `clang-tidy.log` at the repo root, since the terminal
buffer often can't hold all of it.

### Run `clang-format`

```bash
./scripts/clang-format.sh
```

With no arguments, runs against every relevant file in the repository against
the rules in `.clang-format`.

```bash
./scripts/clang-format.sh --fix                                # Rewrite files in place
./scripts/clang-format.sh src/room.cpp include/livekit/room.h  # Check just these files
./scripts/clang-format.sh --fix src/room.cpp                   # Fix just this file
```

Output is captured to `clang-format.log` at the repo root.

---

## Pre-commit hook

A simple pre-commit hook that auto-formats staged C/C++ files using the
project's `.clang-format` rules:

```bash
./cpp-tools/install.sh precommit-hook
```

This installs `.git/hooks/pre-commit`. Re-run after `git clone` on a fresh
checkout.

---

## Memory checks (valgrind)

Run `valgrind` against the integration or stress test binaries to check for
memory leaks and other issues:

```bash
valgrind --leak-check=full ./build-debug/bin/livekit_integration_tests
valgrind --leak-check=full ./build-debug/bin/livekit_stress_tests
```

`valgrind` is Linux-only. On macOS, use `leaks` or Instruments instead.

---

## API documentation (Doxygen)

API reference is generated from headers using Doxygen. To rebuild locally:

```bash
./scripts/generate-docs.sh
```

Output lands under `docs/doxygen/html/`. The deployed reference is at
[docs.livekit.io/reference/client-sdk-cpp/](https://docs.livekit.io/reference/client-sdk-cpp/).

To view the generated documentation locally, open `docs/doxygen/html/index.html` in your browser.

For details on the Doxygen configuration and CI pipeline, see the
[doxygen/](https://github.com/livekit/client-sdk-cpp/tree/main/docs/doxygen) folder.

---

## Development tips

### Bump the pinned Rust submodule

```bash
cd client-sdk-cpp
git fetch origin
git switch -c try-rust-main origin/main

# Sync submodule URLs and check out what origin/main pins (recursively):
git submodule sync --recursive
git submodule update --init --recursive --checkout

# If the nested submodule under yuv-sys didn't materialize, force it:
git -C client-sdk-rust/yuv-sys submodule sync --recursive
git -C client-sdk-rust/yuv-sys submodule update --init --recursive --checkout

# Sanity check:
git submodule status --recursive
```

### If `yuv-sys` fails to build

```bash
cargo clean -p yuv-sys
cargo build -p yuv-sys -vv
```

### Full clean (Rust + C++ build folders)

To delete all build artifacts from both Rust and C++ folders, plus the
local-install folder:

```bash
./build.sh clean-all
```
