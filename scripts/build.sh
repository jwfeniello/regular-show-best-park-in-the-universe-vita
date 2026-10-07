#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
export VITASDK="${PARK_VITASDK:-$HOME/.local/share/scrib-vitasdk-softfp}"
export PATH="$VITASDK/bin:$PATH"
if [[ ! -x "$VITASDK/bin/arm-vita-eabi-gcc" ]]; then
    echo 'Run scripts/setup-sdk.sh first.' >&2
    exit 1
fi
# Build on WSL's filesystem: avoids Windows path and timestamp issues in make.
work="${PARK_BUILD_ROOT:-$HOME/.cache/best-park-vita/build-project}"
mkdir -p "$work/source" "$project_dir/build"
tar -C "$project_dir" --exclude=.git --exclude='*.o' --exclude='*.a' \
    -cf - CMakeLists.txt source lib extras | tar -C "$work/source" -xf -
# Apply the UTF-8 fix to a clean submodule checkout; accept an already fixed copy.
patch_file="$project_dir/patches/falsojni-utf8.patch"
# Git for Windows can check out this dependency with CRLF line endings.
sed -i 's/\r$//' "$work/source/lib/falso_jni/FalsoJNI.c"
# --force prevents patch from silently retrying a reverse probe forwards.
if patch --silent --batch --force --dry-run --reverse -p1 -d "$work/source/lib/falso_jni" < "$patch_file" >/dev/null 2>&1; then
    :
else
    patch --batch --forward -p1 -d "$work/source/lib/falso_jni" < "$patch_file"
fi
cmake -S "$work/source" -B "$work/build" -DCMAKE_BUILD_TYPE=Debug \
    -DPARK_DIAGNOSTICS="${PARK_DIAGNOSTICS:-OFF}"
cmake --build "$work/build" --parallel 4 2>&1 | tee "$project_dir/build/build.log"
cp "$work/build/best_park_vita.vpk" "$work/build/best_park_vita" \
   "$work/build/eboot.bin" "$project_dir/build/"
