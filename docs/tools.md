# Developer tools

This SDK uses several tools and checks to enforce code quality. All of these
are also enforced in CI on PRs.

## Clang tools

`clang-tidy` and `clang-format` are owned by the
[`livekit/cpp-tools`](https://github.com/livekit/cpp-tools) submodule. The
`./cpp-tools/install.sh` script creates root `.clang-tidy` and `.clang-format`
symlinks so editor integrations can discover them automatically, and installs
the pre-commit auto-formatter.
The workflow visible at `cpp-tools/.github/workflows/cpp-tools.yml` belongs to
the submodule, not the SDK. GitHub loads that workflow from the separate
`livekit/cpp-tools` repository at an immutable commit, then checks out this SDK
and runs the scripts from its submodule. The workflow ref and submodule gitlink
must point to the same commit. Doxygen remains in the SDK-specific
documentation workflow.

- **`clang-tidy`** — static analysis. See `cpp-tools/.clang-tidy` for the
  enabled checks. Enforced in CI on PR.
- **`clang-format`** — code formatting and style consistency. See
  `cpp-tools/.clang-format` for the rules. Enforced in CI on PR.

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

Install the shared configuration symlinks and pre-commit hook from the
repository root:

```bash
./cpp-tools/install.sh
```

### Run `clang-tidy`

1. Generate `compile_commands.json` and the protobuf headers via a release build:

   ```bash
   ./build.sh release
   ```

2. Run the shared script with the same file set, regex filters, and
   `.clang-tidy` config as CI:

   ```bash
   ./cpp-tools/clang-tidy.sh \
     --build-dir build-release \
     --file-regex '^(?!.*/(_deps|build-[^/]*|client-sdk-rust|cpp-example-collection|vcpkg_installed|docker|docs|data)/).*/src/(?!tests/).*\.(c|cpp|cc|cxx)$' \
     --header-filter '.*/(include/livekit|src)/.*\.(h|hpp)$' \
     --exclude-header-filter '(.*/src/tests/.*)|(.*/_deps/.*)|(.*/build-[^/]*/.*)' \
     --require-generated-protobuf build-release/generated
   ```

Pass source files positionally to limit analysis, `-j N` to override the worker
count, or `--fix` to apply fixes.

Output is captured to `clang-tidy.log` at the repo root, since the terminal
buffer often can't hold all of it.

### Run `clang-format`

```bash
./cpp-tools/clang-format.sh --path src --path include --path benchmarks
```

```bash
./cpp-tools/clang-format.sh --path src --path include --path benchmarks --fix
./cpp-tools/clang-format.sh src/room.cpp include/livekit/room.h
./cpp-tools/clang-format.sh --fix src/room.cpp
```

Output is captured to `clang-format.log` at the repo root.

---

## Pre-commit hook

A simple pre-commit hook that auto-formats staged C/C++ files using the
project's `.clang-format` rules is included by the default installation. To
install only the hook:

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
