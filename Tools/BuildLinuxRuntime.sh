#!/usr/bin/env bash
set -euo pipefail
# Native Linux x64 build. Needs .NET 8 SDK, Vulkan SDK (including dxc), CMake,
# a C++20 compiler with std::format (GCC 13+), X11/Wayland development packages.
engine_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="${1:-$engine_root/build-linux}"
cmake -S "$engine_root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DSTARDUST_BUILD_EDITOR=OFF
cmake --build "$build_dir" --target StardustPlayer -j "$(nproc)"
runtime="$build_dir/bin/Release"
mkdir -p "$runtime/Linux"
cp "$runtime/StardustPlayer" "$runtime/libnethost.so" "$runtime/engineIcon.png" "$runtime/Linux/"
cp "$runtime/Stardust.ScriptCore.dll" "$runtime/Stardust.ScriptCore.runtimeconfig.json" "$runtime/Stardust.ScriptCore.deps.json" "$runtime/Linux/"
echo "Copy $runtime/Linux to the Windows editor's build/bin/Release/Linux directory."
