# Repository Guidelines

## Project Structure & Module Organization

Omnivox is a Rust 2021 Cargo workspace for a cross-platform Emacspeak speech server:

- `omnivox-core/`: Emacspeak protocol parsing, queue management, and shared state.
- `omnivox-tts/`: TTS engine trait and macOS, Windows, espeak-ng, and optional Piper backends.
- `omnivox-audio/`: stereo `f32` buffers, effects, tones, file loading, output, and integration tests.
- `omnivox-cli/`: `omnivox` binary wiring protocol input, synthesis, and playback.
- `omnivox-piper-sys/`: optional C/C++ Piper bridge; excluded from default workspace builds.

Supporting files include `elisp/` for Emacs integration, `docs/`, `test-sounds/` fixtures, and `tools/` diagnostics.

## Build, Test, and Development Commands

Standard contributors can run commands directly; Codex agents should prefix shell commands with `rtk`, such as `rtk make test`.

- `make dev`: debug build.
- `make build`: release build.
- `make test`: run the full Rust test suite.
- `make lint`: run `cargo clippy -- -D warnings`.
- `make fmt`: apply `cargo fmt`.
- `make check`: type-check without producing final binaries.
- `make install`: install `omnivox` to `~/.cargo/bin`.
- `make build-piper`: build `omnivox-cli` with `--features piper`; requires CMake, C++17, and first-run network access.

Use targeted commands while iterating, for example `cargo test -p omnivox-audio loader`.

## Coding Style & Naming Conventions

Use Rust 2021 defaults and `cargo fmt`; keep indentation and imports rustfmt-managed. Prefer `snake_case` for modules, functions, and tests; `PascalCase` for types and traits; and `SCREAMING_SNAKE_CASE` for constants. Keep platform code in existing backend modules and build scripts. Avoid hot-path text rechunking unless the protocol contract changes.

## Testing Guidelines

Place unit tests in local `#[cfg(test)] mod tests` blocks near the implementation. Put cross-module tests under crate-level `tests/`, as in `omnivox-audio/tests/integration_tests.rs`. Test names should describe behavior or regressions, especially protocol parsing, audio transforms, queue interruption, and platform mapping. Run `make test` before submitting, plus `make lint` for shared code.

## Commit & Pull Request Guidelines

Commit history uses short imperative subjects such as `Fix macOS audio quality...`, `Add Piper neural TTS backend...`, and `Document espeak-ng fallback behavior...`. Keep subjects specific and scoped. PRs should include a brief problem/solution summary, linked issue when applicable, tested platforms, and exact output for `make test`, `make lint`, or targeted tests. Include audio samples or logs only when they clarify behavior.

## Security & Configuration Tips

Do not commit generated WAV/PCM debug files or build artifacts. Document behavior controlled by `OMNIVOX_ENGINE=espeak` and `OMNIVOX_AUDIO_TARGET=left|right|both`. Treat Piper downloads and native library paths as optional configuration, not default-build assumptions.
