"""Run the actual controller bridge against native-game doubles on WSL x86-64."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="park-controls-") as directory:
    work = Path(directory)
    (work / "utils").mkdir()
    (work / "reimpl").mkdir()
    for name in ("gamepad.c", "gamepad.h"):
        shutil.copyfile(ROOT / "source" / name, work / name)
    shutil.copyfile(ROOT / "source/reimpl/controls.h", work / "reimpl/controls.h")
    shutil.copyfile(ROOT / "tests/controller_test.c", work / "test.c")
    (work / "park.h").write_text("#include <stdint.h>\n#include <stdbool.h>\nuintptr_t park_symbol(const char *name);\n")
    (work / "utils/logger.h").write_text("#define l_info(...) ((void)0)\n")
    (work / "utils/dialog.h").write_text("#include <stdio.h>\n#include <stdlib.h>\n#define fatal_error(...) do {fprintf(stderr,__VA_ARGS__);abort();}while(0)\n")
    binary = work / "controller-test"
    subprocess.run(["gcc", "-O2", "-Wall", str(work / "test.c"), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
