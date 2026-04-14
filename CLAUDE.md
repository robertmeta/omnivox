# CLAUDE.md - Omnivox Project

## Project Overview

Omnivox is a cross-platform speech server written in Rust. It is the recommended backend for **emacsvox** (the modern Emacspeak fork) and a drop-in replacement for any emacspeak/dtk server (`dtk-soft`, `swiftmac`, etc.) on macOS, Linux, and Windows.

**Design principle (from emacsvox):** "emacsvox decides WHAT to speak. Omnivox decides HOW to speak it."
- Omnivox trusts that each `q {}` item it receives is already a correctly-sized chunk (clause/sentence level)
- Omnivox does NOT rechunk text by word count — that would break prosody at arbitrary boundaries
- All configuration happens via protocol commands after startup; no CLI flags are needed for normal use

## Architecture

4-crate Cargo workspace:

- **omnivox-core** - Command parsing (27 Emacspeak protocol commands), queue management, state types. Pure Rust, no platform dependencies.
- **omnivox-tts** - TTS engine trait + backends. `TtsEngine::synthesize()` returns an `AudioBuffer`. Backends: macOS AVSpeechSynthesizer (via ObjC bridge), Windows WinRT SpeechSynthesizer (via windows-rs), espeak-ng (via espeak-rs-sys). espeak-ng is always compiled in as cross-platform fallback.
- **omnivox-audio** - Audio buffer (stereo f32 @ 44100Hz canonical format), effects pipeline (`AudioEffect` trait), tone generator, OGG/WAV file loader with LRU cache, rodio-based output.
- **omnivox-cli** - Main binary wiring everything together. Reads Emacspeak protocol from stdin, dispatches to TTS/tone/audio-icon sources, runs through pipeline, plays via rodio.

### Key Design Decisions

- **No word-count chunking in hot paths**: Each `q {}` item from emacsvox/emacspeak is already clause-level. `process_batch()` synthesizes each Speech item as a single unit. The ObjC bridge accumulates ALL AVSpeechSynthesizer callbacks into one buffer anyway, so there is no "single-buffer" benefit to word-count splitting — only prosody breakage. `chunk_text()` in `text.rs` is retained but marked `#[allow(dead_code)]`.
- **Stereo f32 @ 44100Hz** is the universal internal buffer format. All sources convert to this before pipeline processing.
- **Everything statically linked** - no external process calls (no sox, no CLI tools).
- **espeak-ng always compiled in** (not feature-gated) as guaranteed cross-platform fallback.
- **ObjC bridge** for macOS: `macos_bridge.m` compiled via `cc` crate in build.rs. This was necessary because Rust `block` crate produces blocks incompatible with AVSpeechSynthesizer's `writeUtterance:toBufferCallback:`.
- **Persistent singleton AVSpeechSynthesizer** in the ObjC bridge for stop() support.
- **AudioEffect trait** with `Vec<Box<dyn AudioEffect>>` pipeline for extensible processing.
- **rubato** sinc resampler (256-tap BlackmanHarris2 window) for 22050Hz→44100Hz upsampling. Replaced naive linear interpolation which caused audible wobble.
- **rodio** for cross-platform audio output and OGG/WAV decoding.
- **OMNIVOX_ENGINE=espeak** environment variable to force espeak-ng on macOS.
- **OMNIVOX_AUDIO_TARGET** environment variable for channel routing (left/right/both). Used by Emacspeak for dual-server notification mode.
- **Three-thread model (macOS)**: `main()` runs the NSRunLoop on the main thread (required by AVSpeechSynthesizer). A reader thread processes stdin. A synthesis worker thread receives `SynthRequest` via `mpsc::channel`, synthesizes chunks, checks generation counter, queues audio. On non-macOS, reader runs on main thread (two-thread model). Stop commands (`s`) take effect in microseconds.
- **macOS RunLoop requirement**: `AVSpeechSynthesizer.writeUtterance:toBufferCallback:` internally dispatches via the main GCD queue. If the main thread blocks on raw I/O instead of running a NSRunLoop, synthesis deadlocks. Fixed by spawning the reader on a background thread and calling `[[NSRunLoop mainRunLoop] run]` on main. `omnivox_run_main_runloop()` / `omnivox_stop_main_runloop()` in `macos_bridge.m` manage the lifecycle.
- **Generation counter** (`Arc<AtomicU64>`): incremented on every stop/interrupt. Worker checks `gen_counter.load() != request_gen` before and after each `engine.synthesize()` call; stale results are discarded without queuing.
- **`AudioControl`** (`Arc<Sink>` fields, `Send+Sync`): split from `AudioStreams` so sinks can be shared with the worker thread. `AudioStreams` (owns `!Send OutputStream`) stays on main thread; worker holds `Arc<AudioControl>`.
- **espeak `stop()` no-lock**: `espeak_Cancel()` called without acquiring `ESPEAK_LOCK` — avoids deadlock when reader calls `stop()` while worker holds the lock in `synthesize()`.
- **`engine.stop()` only on hard stop**: `interrupt()` takes a `stop_engine: bool` parameter. `TtsSay` and `Letter` pass `false` — the generation counter already discards stale results, and cross-thread `[synth stopSpeakingAtBoundary:immediate]` corrupts AVSpeechSynthesizer state. Only the `s` (Stop) and reset commands pass `true`.

### Audio Pipeline Flow

```
Source (TTS / Tone / File) -> AudioBuffer (stereo f32 @ 44100Hz) -> Effects Pipeline -> AudioStreams
                                                                         |                    |
                                                              SilenceTrimmer (speech)   Speech Sink (max 10)
                                                              VolumeAdjust              Tone Sink (max 3)
                                                              ChannelRouter             Sound Sink (max 5)
```

Three concurrent audio streams (rodio Sinks on same OutputStreamHandle, automatically mixed):
- **Speech**: TTS output + silence gaps. Serialized within stream.
- **Tones**: Beeps. Serialized within stream, concurrent with speech/sounds.
- **Sounds**: Audio icons. Serialized within stream, concurrent with speech/tones.

Each stream has a max backlog depth. On overflow, old items are dropped to keep audio current.

## Build Commands

```bash
make build     # Release build
make dev       # Debug build
make test      # Run all tests (170 tests)
make lint      # Clippy
make fmt       # Format
make install   # Install binary to ~/.cargo/bin
make clean     # Clean build artifacts
```

## Testing

173 tests total, all passing:

- omnivox-audio: 60 unit + 31 integration = 91
- omnivox-core: 37 unit + 1 doc = 38 (includes `;;` regression tests)
- omnivox-tts: 22 unit (includes WinRT mapping + WAV header tests)
- omnivox-cli: 16 unit

Run: `cargo test`

## Audio Debug Tools

### CLI Flags

```bash
# Dump TTS output to WAV files (raw + pipeline-processed)
omnivox --dump-wav 'en-US:Alex' alex.wav Hello world

# Play a WAV file through the rodio audio path (same as normal speech)
omnivox --play-wav alex.wav
```

`--dump-wav` saves two files: `<name>_raw.wav` (post-resample, pre-pipeline) and `<name>.wav` (post-pipeline with silence trimming, volume, channel routing). Useful for isolating whether an audio issue is in synthesis, resampling, or the effects pipeline.

`--play-wav` plays a WAV through the same rodio sink path as normal speech. Useful for A/B testing against `afplay` to isolate rodio playback issues.

### tools/ Directory

- **tools/tts_reference.swift** - Generate reference WAV files using AVSpeechSynthesizer directly (bypassing omnivox). Uses the same API and parameters as omnivox's ObjC bridge. Produces both raw (native sample rate) and resampled (44100Hz) versions.
- **tools/compare_wavs.py** - Numerically compare two WAV files: RMS difference, correlation, SNR, per-segment analysis, and amplitude modulation detection.

```bash
# Generate reference WAV
swift tools/tts_reference.swift "Alex" ref_alex.wav "Hello world"

# Compare omnivox output vs reference
python3 tools/compare_wavs.py omnivox.wav reference.wav
```

## Manual Testing

```bash
# Basic speech — IMPORTANT: use printf+sleep, NOT echo.
# echo closes stdin immediately, dropping the OutputStream before audio plays.
# The sleep keeps stdin open long enough for audio to finish.
(printf 'tts_say {Hello world}\n'; sleep 5) | ./target/release/omnivox

# espeak-ng engine
(printf 'tts_say {Hello world}\n'; sleep 5) | OMNIVOX_ENGINE=espeak ./target/release/omnivox

# List voices
omnivox --list-voices

# Diagnostic self-test
omnivox --check
```

## Platform Status

| Platform | Native TTS | espeak-ng | Status |
|----------|-----------|-----------|--------|
| macOS | AVSpeechSynthesizer (ObjC bridge) | Compiled in | Working |
| Linux | Not yet (Speech Dispatcher planned) | Compiled in | espeak-ng works |
| Windows | WinRT SpeechSynthesizer (via windows-rs) | Compiled in | Working |

## Current State (as of 2026-02-18)

### Working

- Full Emacspeak protocol (27 commands)
- macOS native TTS via AVSpeechSynthesizer buffer capture (ObjC bridge)
- Windows native TTS via WinRT SpeechSynthesizer (windows-rs, WAV stream capture)
- espeak-ng TTS (always compiled in, cross-platform)
- Audio pipeline: silence trimming, volume adjust, channel routing
- Tone generation (pure Rust sine wave with fade envelopes)
- Audio icon playback (OGG/WAV via rodio, with LRU cache)
- Punctuation replacement (none/some/all levels)
- Split caps, letter speaking with pitch raise
- Voice switching (intra-sentence)
- Stop/reset with persistent synthesizer

### Not Yet Implemented

- **Linux Speech Dispatcher backend** - For native Linux voices (espeak-ng works as fallback).
- **Network mode** (-p flag for TCP listener)
- **Multi-device audio routing**
- **Sox-style effects** (reverb, echo, chorus)
- **Language switching tables**

### Evaluated and Rejected

- **Piper neural TTS** - Evaluated as an optional backend for high-quality offline neural voices via ONNX Runtime. The `piper-rs` crate (v0.1.9) has a broken dependency chain: it pins `ndarray 0.16` but its `ort` dependency resolves to a version requiring `ndarray 0.17`, causing compile failures. The alternative `piper-tts-rust` crate is English-only and uses a limited g2p model. Piper integration should be revisited if/when the Rust crate ecosystem stabilizes.

## Troubleshooting: Unexpected espeak-ng Voice

espeak-ng is **always compiled in** as an unconditional fallback. If omnivox starts
speaking with a robotic espeak-ng voice when you expect native macOS/Windows TTS,
this is almost always a **configuration problem**, not a compile failure.

### Diagnosis

Run `omnivox --check` — the `[engine]` section shows which engine was actually selected
and will print any warnings that caused a fallback.

Common causes and fixes:

- **`OMNIVOX_ENGINE=piper` (or `espeak`) set in Emacs init.el** — If `OMNIVOX_ENGINE`
  is set to a value that fails (e.g. `piper` without a `--features piper` build, or
  `piper` with a missing/bad model path), omnivox silently falls back to espeak-ng.
  Comment out the `setenv` call in your init.el. macOS native TTS is the default; no
  env var is needed to enable it.

- **`OMNIVOX_PIPER_MODEL` missing or invalid path** — If `OMNIVOX_ENGINE=piper` but
  the model file doesn't exist, omnivox warns and falls back to espeak-ng.

- **Build was done without native TTS support** — Unlikely on macOS (the ObjC bridge
  always compiles), but `cargo build` vs `make build` should both include it.

### Engine selection order

1. If `OMNIVOX_ENGINE=espeak`, use espeak-ng directly (intentional override).
2. If `OMNIVOX_ENGINE=piper`, try piper; fall back to espeak-ng on failure.
3. Otherwise: try native (macOS AVSpeechSynthesizer / Windows WinRT); fall back to
   espeak-ng only if native init fails.
4. espeak-ng is the final fallback and will always succeed.

## Key Files

- `omnivox-tts/src/lib.rs` - TtsEngine trait definition. New backends must implement `synthesize() -> Result<AudioBuffer, TtsError>`.
- `omnivox-tts/src/espeak.rs` - espeak-ng backend (cross-platform fallback).
- `omnivox-tts/src/macos.rs` - macOS AVSpeechSynthesizer backend (ObjC bridge).
- `omnivox-tts/src/windows.rs` - Windows WinRT backend (SpeechSynthesizer via windows-rs).
- `omnivox-cli/src/main.rs` - Main binary. Two-thread architecture: `run_server()` is the reader loop; `synthesis_worker()` runs on a spawned thread receiving `SynthRequest` via mpsc channel. `create_engine()` returns `Arc<dyn TtsEngine>`. `SynthCtx` groups worker context to reduce argument counts. `interrupt()` handles stop/preempt from reader thread.
- `elisp/omnivox-voices.el` - Emacs voice module. Self-registering via advice on `voice-setup` and `dtk-speak`. Provides defcustoms, interactive commands, and voice querying.

## Dependencies

Key workspace dependencies (Cargo.toml):

- thiserror, anyhow, tracing, tracing-subscriber, regex, once_cell
- omnivox-tts: espeak-rs-sys (with compile-espeak-intonations), rubato (sinc resampler), cc (build), windows v0.58 (Windows-only, WinRT SpeechSynthesizer)
- omnivox-audio: rodio (vorbis + wav features)
- omnivox-cli: all workspace crates (no tokio — pure std threads)

## Dual AudioBuffer Types

There are two `AudioBuffer` types (technical debt):

- `omnivox_tts::AudioBuffer` - in omnivox-tts/src/lib.rs (TTS output format)
- `omnivox_audio::AudioBuffer` - in omnivox-audio/src/buffer.rs (pipeline format)

Both are stereo f32 @ 44100Hz but are separate types. The CLI converts between them via `tts_buffer_to_audio_buffer()` in main.rs. A future cleanup could unify these.

## Environment Variables

See [ENV-VARS.md](ENV-VARS.md) for complete documentation.

- **OMNIVOX_ENGINE** - Set to `espeak` to force espeak-ng engine
- **OMNIVOX_AUDIO_TARGET** - Set to `left`, `right`, or `both` for channel routing. Read at startup in main.rs, passed to `ChannelRouter` effect. Used by Emacspeak for dual-server notification mode (notification server gets `OMNIVOX_AUDIO_TARGET=left`, main server uses both channels).

## Emacsvox Integration (Primary User)

`elisp/omnivox-voices.el` is a self-registering Emacs module. It supports both **emacsvox** (the modern fork) and **emacspeak** (classic).

### emacsvox Setup

emacsvox invokes omnivox as a plain subprocess — no CLI args. All settings come via protocol after startup. Add to init.el **before** emacsvox loads:

```elisp
(add-to-list 'load-path "/path/to/omnivox/elisp")
(require 'omnivox-voices)
(setq omnivox-voice-id "en-US:Alex")    ; optional: pick a voice
(setq omnivox-speech-rate 60)           ; optional: 0-100, 50=normal
(setenv "EMACSVOX_TTS_PROGRAM" "omnivox")
(require 'emacsvox)
```

emacsvox already auto-loads `plain-voices.el` for the omnivox backend. Loading `omnivox-voices.el` overrides this so pitch/voice changes (bold, italic, etc.) are sent as `[[pitch N]]` and `[{voice NAME}]` inline codes that omnivox understands. Without `omnivox-voices.el`, voice locking is silent no-ops.

### How Self-Registration Works

- `with-eval-after-load 'voice-setup` adds `:around` advice on `voice-setup`; when `dtk-program` matches "omnivox", calls `omnivox-configure-tts`
- `omnivox-configure-tts` sets BOTH `tts-*` (emacspeak) and `emacsvox-tts-*` (emacsvox) voice dispatch functions — required because emacsvox-tts.el uses defalias chains that are one-directional
- `with-eval-after-load 'emacsvox-tts` and `with-eval-after-load 'dtk-speak` each add "omnivox" to `tts-multi-engines`
- Protocol sends initial settings (`tts_set_speech_rate`, `tts_set_pitch_multiplier`, volumes) immediately after the process starts

### emacspeak / DTK Migration

Emacspeak users who currently use `dtk-soft` (software DECtalk) or `swiftmac` can switch to omnivox by changing one setting. The wire protocol is identical (`q {}`, `d`, `s`, `tts_say {}`, etc.). Load omnivox-voices.el to get proper voice codes instead of DECtalk-format codes:

```elisp
(add-to-list 'load-path "/path/to/omnivox/elisp")
(require 'omnivox-voices)
(setq dtk-program "omnivox")
(require 'emacspeak-setup)
```

Ensure the omnivox binary is in PATH or symlinked into emacspeak/servers/.

### Windows-Specific Setup

1. **HOME env var**: Set `HOME=C:\Users\<username>` so Emacs finds `~/.emacs.d/init.el`
2. **Copy binary**: Copy `omnivox.exe` into `~/.emacspeak/servers/` (symlinks require admin on Windows)
3. **Generate emacspeak-loaddefs.el**: `emacs --batch -l ./emacspeak-preamble.el -l ./emacspeak-autoload.el -f emacspeak-auto-generate-autoloads` from `~/.emacspeak/lisp/`
4. **LIBCLANG_PATH**: Set `LIBCLANG_PATH=C:\\LLVM\\bin` before building

### `;;` Text Handling

- **Parser is correct** — regression tests in `omnivox-core/src/command.rs` (`test_parse_semicolons_*`, `test_parse_dtk_speak_format`) confirm the regex preserves all text including `;;` and content after it.
- **If text after `;;` appears silently dropped**: the bug would be in the TTS engine layer (macOS ObjC bridge or espeak-ng), not the parser. Investigate with `omnivox --dump-wav`.

### Dual-Server Notification Mode

When notification mode is enabled, two omnivox processes are spawned:

1. Main process - Uses both channels for primary speech
2. Notification process - Emacspeak sets `OMNIVOX_AUDIO_TARGET=left`; emacsvox uses `emacsvox-tts-notify-initialize()` and the `tts_set_speech_channel` protocol command

This enables concurrent notifications (e.g., "50 percent" in left ear) while main content continues in both ears.
