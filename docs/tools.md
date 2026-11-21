# Developer tools

This SDK uses several tools and checks to enforce code quality. All of these
are also enforced in CI on PRs.

## Clang tools

The shared configuration and tooling scripts come from the
[`livekit/cpp-tools`](https://github.com/livekit/cpp-tools) submodule.

These developer tools are versioned independently from the compiler used to
build the SDK:

- Linux SDK builds require clang 21 or later.
- Formatting CI uses clang-format 22.
- Static-analysis CI uses clang-tidy 19 (the configuration requires at least
  version 19).
- macOS SDK builds use Xcode Apple Clang, not Homebrew LLVM.

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

CI remains the reference for exact formatter and analyzer versions when the
current Homebrew LLVM release differs.

**Linux (Ubuntu/Debian):**

```bash
# Configure the apt.llvm.org signing key as described in building.md, then add
# the two developer-tool repositories:
codename=$(lsb_release -cs)
for version in 19 22; do
  echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${codename}/ llvm-toolchain-${codename}-${version} main" \
    | sudo tee "/etc/apt/sources.list.d/llvm-${version}.list" >/dev/null
done
sudo apt-get update
sudo apt-get install clang-format-22 clang-tidy-19 clang-tools-19
sudo ln -sf /usr/bin/clang-format-22 /usr/local/bin/clang-format
sudo ln -sf /usr/bin/clang-tidy-19 /usr/local/bin/clang-tidy
sudo ln -sf /usr/bin/run-clang-tidy-19 /usr/local/bin/run-clang-tidy
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

Update to the commit from the Rust SDK release with the newest publication
time (including prereleases, excluding drafts):

```bash
./scripts/update-rust-sdk.sh
```

The script requires an authenticated GitHub CLI (`gh auth login`, or `GH_TOKEN`)
when `--hash` is omitted. To select a specific commit without `gh`, pass its full
or abbreviated hash:

```bash
./scripts/update-rust-sdk.sh --hash 1a477bc
```

The script checks out the selected Rust commit and synchronizes its nested
submodules. It leaves the parent index untouched; review and stage the gitlink
when ready. `--hash` also permits unreleased commits for local development; CI
requires a published release for the pinned Rust commit.

Review the selected commits before committing the gitlink update:

```bash
git diff --submodule=log -- client-sdk-rust
git add -- client-sdk-rust
```

### Bump the pinned C++ example collection

Update to the most recent commit on the `main` branch of the examples
repository:

```bash
./scripts/update-cpp-example-collection.sh
```

To select a specific commit, pass its full or abbreviated hash:

```bash
./scripts/update-cpp-example-collection.sh --hash 7e76bea
```

Review and stage the examples gitlink when ready:

```bash
git diff --submodule=log -- cpp-example-collection
git add -- cpp-example-collection
```

Both updaters run from any working directory, initialize the selected submodule
if necessary, detach its checkout at the selected commit, and synchronize nested
submodules using checkout mode.

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
