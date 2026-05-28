# Guides

Hand-written long-form documentation for the LiveKit C++ SDK. These
files are processed by Doxygen as additional pages and surface in the
generated docs alongside the auto-extracted API reference under
`include/livekit/`.

This directory complements:

- **`include/livekit/*.h`** — `///` doc comments for the API reference
  (classes, structs, free functions, enums).
- **`docs/doxygen/index.md`** — the Doxygen mainpage / landing page
  that links out to everything else.
- **`docs/doxygen/`** — Doxygen tool configuration (`Doxyfile`, theme
  assets, mainpage). Nothing in there is intended for human reading
  on its own (the mainpage uses Doxygen-only syntax like `@ref`).

## What belongs here

Topics that are too long for a `///` comment on a single type and too
narrative for the API reference. Examples:

- Threading model and lifecycle conventions.
- End-to-end "publish a track" or "subscribe to a stream" walkthroughs.
- Error-handling philosophy (`Result<T, E>` vs exceptions vs callbacks).
- E2EE / key provider deep dives.
- Migration guides between SDK versions.

Anything that is genuinely *about a single symbol* belongs as a `///`
comment on that symbol instead, so it shows up inline in IDEs.

## File convention

- One topic per file. Filename in `kebab-case.md`.
- First line is a top-level `# Title` heading (becomes the Doxygen
  page title).
- Use `///`-style Doxygen commands sparingly inside Markdown — prefer
  plain Markdown where it works.
- Reference public symbols with `@ref livekit::FooBar` (classes/structs)
  or `@ref livekit::someFreeFunction()` (free functions; the parens
  are required for Doxygen to resolve the name).

## Wiring into Doxygen

Files in this directory are **not yet wired into the Doxyfile's
`INPUT`**. When the first real guide lands, add `docs/guides` to the
`INPUT =` line of `docs/doxygen/Doxyfile` so Doxygen processes the
folder. This `README.md` is intentionally excluded until then so it
doesn't render as a "README" page in the generated site.
