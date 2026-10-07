"""Run production audio/save code with real sndfile and pthread-backed Vita APIs.

Injects 250 ms reads/writes and a failed save commit. Requires WSL libsndfile-dev.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="park-audio-saves-") as folder:
    work = Path(folder)
    for name in ("utils", "psp2/kernel", "data/saves"):
        (work / name).mkdir(parents=True)
    for name in ("audio.c", "audio.h", "assets.h", "saves.c", "saves.h"):
        shutil.copyfile(ROOT / "source" / name, work / name)
    for name in ("host_vita.h", "audio_save_test.c"):
        shutil.copyfile(ROOT / "tests" / name, work / name)
    (work / "psp2/kernel/threadmgr.h").write_text('#include "host_vita.h"\n')
    (work / "psp2/audioout.h").write_text('#include "host_vita.h"\n')
    (work / "utils/logger.h").write_text('#define l_info(...) ((void)0)\n#define l_warn(...) ((void)0)\n#define l_error(...) ((void)0)\n')
    (work / "utils/dialog.h").write_text('#include <stdio.h>\n#include <stdlib.h>\n#define fatal_error(...) do {fprintf(stderr,__VA_ARGS__);abort();}while(0)\n')
    fixture = work / "test.wav"
    with wave.open(str(fixture), "wb") as stream:
        stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
        stream.writeframes(b"\xe8\x03\x18\xfc" * 44100)
    binary = work / "test"
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sndfile"], text=True).split()
    subprocess.run(["gcc", "-O2", "-g", "-Wall", "-Wextra", "-Wno-misleading-indentation",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(work),
                    '-DDATA_PATH="' + str(work / "data") + '/"', '-DHOST_WAV="' + str(fixture) + '"',
                    str(work / "audio_save_test.c"), "-pthread", "-lm", *flags, "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=15,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
