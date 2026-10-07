# Third-party code and artwork

This port builds on the [soloader boilerplate](https://github.com/v-atamanenko/soloader-boilerplate) and compatibility work from the earlier [Scribblenauts Remix](https://github.com/jwfeniello/scribblenauts-remix-vita) and [Amazing Alex](https://github.com/jwfeniello/Amazing-Alex-Vita) Vita ports. Existing copyright and license notices are preserved in the source files and root [LICENSE](LICENSE).

Dependencies are vendored as source, including local changes:

| Dependency | License |
| --- | --- |
| [FalsoJNI](https://github.com/v-atamanenko/FalsoJNI) | [MIT](lib/falso_jni/LICENSE) |
| [FalsoNDK](https://github.com/elliencode/FalsoNDK) | [Apache 2.0](lib/falso_ndk/LICENSE) |
| [so_util](https://github.com/Rinnegatamante/so_util) | [MIT](lib/so_util/LICENSE) |
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) | [LGPLv3](lib/vitagl/COPYING.LESSER), with [GPLv3 text](lib/vitagl/COPYING) |
| [stb_truetype](https://github.com/nothings/stb) | License choices in [the header](lib/stb/stb_truetype.h) |

FalsoJNI includes UTF-8 fixes and host adapters. vitaGL includes inherited texture-stage and shader changes; the complete modified source is under `lib/vitagl/`. The patches in `patches/` retain selected earlier changes for reference. Unrelated vitaGL sample programs and their media are omitted.

Other compatibility sources under `lib/` and `source/` retain their original notices. SDK libraries are installed by `scripts/setup-sdk.sh` from the [softfp VitaSDK packages](https://github.com/vitasdk-softfp/packages).

Regular Show characters, logos and game artwork remain the property of their respective owners. The LiveArea artwork was generated with AI using supplied game and character references; masters and prompts are in `extras/livearea/`. These artwork assets are not covered by the source-code license.

The Android APK, native game library and playable game data are not included.
