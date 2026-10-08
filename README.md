# Regular Show: Best Park in the Universe for PS Vita

A heavily AI-assisted port of the Android version of **Regular Show: Best Park in the Universe** (v1.2.1), using vitaGL. Includes physical controls, Vita button prompts, music and sound effects, and custom LiveArea artwork.

## Installation

1. Install [kubridge](https://github.com/TheOfficialFloW/kubridge) and `libshacccg.suprx` ([ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D) can install it).
2. Download `best_park_vita.vpk` from [Releases](https://github.com/jwfeniello/regular-show-best-park-in-the-universe-vita/releases) and install it with VitaShell.
3. Obtain your own Android **1.2.1 / versionCode 16** game files. Download this repository, create a `data` folder inside it, and place these files there:

   | File | Source |
   | --- | --- |
   | `game.apk` | Original APK, renamed (5,857,633 bytes) |
   | `libgame.so` | Extract `lib/armeabi/libgame.so` from the APK (5,359,068 bytes) |
   | `main.16.com.turner.bestparkintheuniverse.obb` | Original OBB (388,494,611 bytes) |

4. With Python 3.9 or newer installed, run `python scripts/prepare_data.py` from the repository folder. This creates `data/assets.idx`.
5. Copy all four files into `ux0:data/bestpark/` on your Vita and launch the game.

Game files are not included. Saves are stored in `ux0:data/bestpark/saves/`.

## Controls

| Vita control | Action |
| --- | --- |
| Left stick / D-pad | Move |
| Square | Forward attack; repeat for combos |
| Triangle | Upward combo |
| Cross | Downward combo |
| Circle | Retreat / backward swipe |
| L | Switch character |
| R | Super attack (double-tap action; requires a full meter) |
| Start | Pause / Back |
| Touchscreen | Menus and original gesture controls |

Combo branches use their normal in-game skill unlocks.

## Building

See [PORTING.md](PORTING.md) for build instructions and implementation notes.

## Credits

Original game and characters belong to Cartoon Network and their respective creators. This port builds on vitaGL, FalsoJNI and the Android loader work of the Vita homebrew community. See [THIRD_PARTY.md](THIRD_PARTY.md) for dependencies and licenses.
