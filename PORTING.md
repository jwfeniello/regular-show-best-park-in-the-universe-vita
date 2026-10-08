# Building and porting notes

## Build

Use Linux or WSL/Ubuntu with a soft-float-compatible VitaSDK. Dependencies are vendored as source; no Git submodules are required.

```sh
sudo apt install build-essential cmake curl patch python3 pkg-config libsndfile1-dev
bash scripts/setup-sdk.sh
bash scripts/build.sh
```

The SDK defaults to `$HOME/.local/share/scrib-vitasdk-softfp`, shared with the earlier Android ports. Set `PARK_VITASDK` to use a different location. The setup script downloads the softfp SDK and its library packages separately from a standard VitaSDK installation.

Build output is copied to `build/`, including `best_park_vita.vpk`, `eboot.bin` and the symbol-bearing ELF `best_park_vita`. The working build directory defaults to `$HOME/.cache/best-park-vita/build-project`; set `PARK_BUILD_ROOT` to override it. On WSL, keep that directory on the Linux filesystem.

Logging is disabled by default. For diagnostics, use `PARK_DIAGNOSTICS=ON bash scripts/build.sh`; the runtime log is `ux0:data/bestpark/loader.log`.

## Implementation

- Loads the Android 1.2.1 `armeabi` library with its original soft-float ABI. The game uses Cocos2d-x 2.1.
- Supplies Android/JNI compatibility, ZIP-backed asset access, text rendering, audio and local preferences. Network services and external Android UI are disabled.
- Replaces Linux ARM kuser atomic helpers with ARMv7 equivalents, including the compare/exchange carry flag convention.
- Redirects native PVR texture requests to their matching PNG assets in the original archives.
- Maps the Vita controls into native movement and gesture callbacks from the active gameplay update. Native collision, combos, skill unlocks and super-meter rules remain in use.
- Replaces English help/tutorial/skill instructions and gesture illustrations with Vita controls as the resources load. Inline button symbols use the text renderer, and gesture illustrations stay visible throughout each animation. The original APK and OBB remain unchanged; controller icons are bundled in the VPK.
- Decodes sound effects on a background worker and mixes up to 16 voices with a bounded sample cache. Preference writes also use a background worker with backup and retry handling.
- Renders at 960x544 with a 30 FPS cap. Application title ID: `BPARK0001`.

`scripts/prepare_data.py` generates the small audio/font index without modifying the APK or OBB. `scripts/audit_binary.py` inventories the native library's imports and exports.

## Host checks

Run these in Linux/WSL:

```sh
python3 scripts/test-controls.py
python3 scripts/test-audio-saves.py
python3 scripts/test-prompts.py
python3 scripts/test-text.py
```

These compile the production controller, audio and preference code against host adapters. They cover input dispatch, sound loading and cancellation, cache pressure, delayed I/O, save coalescing and write failures. The audio/save checks use libsndfile and AddressSanitizer/UndefinedBehaviorSanitizer.

The prompt and text checks cover resource replacements, full animation-frame coverage, inline button rendering, wrapping and clipped drawing under AddressSanitizer/UndefinedBehaviorSanitizer. Text previews use the APK's fonts when available, with DejaVu Sans as a fallback.

## Artwork

Final LiveArea PNGs, generation prompts and artwork masters are in `extras/livearea/`. With ImageMagick installed, run `powershell -File scripts/prepare_livearea.ps1` to regenerate the indexed Vita-sized PNGs from the masters.

Controller icons are drawn from vector shapes by `python scripts/prepare_prompts.py`, also using ImageMagick. SVG sources and final PNGs are in `extras/prompts/`.
