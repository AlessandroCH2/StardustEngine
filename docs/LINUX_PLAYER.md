# Linux x64 player export

The Windows editor offers **Build Settings → Player → Target Platform → Linux x64**.
It needs a matching native Linux player runtime at `build/bin/Release/Linux`.
Export validates that this runtime exists, compiles platform-neutral game C#,
cooks the same indexed pak files, copies `libnethost.so`, and creates
`Game.x86_64` plus `Launch.sh`. Build And Run prepares the export but does not try
to execute an ELF binary on Windows. Run `sh Launch.sh` on the target Linux system.

Build the runtime **on Linux x64** using `bash Tools/BuildLinuxRuntime.sh`.
Requirements: .NET 8 SDK, a C++20 compiler with `std::format` (GCC 13+), CMake,
GLFW's X11/Wayland development packages, Vulkan headers/loader and DXC.
The manual GitHub Actions workflow `.github/workflows/linux-player.yml` provides
an Ubuntu 24.04 build recipe and produces the runtime folder as an artifact.
The workflow is supplied locally; it has not been run or published by this task.

Copy the resulting `Linux` folder to the Windows editor's Release directory.
The Linux runtime and editor must come from the same source revision, because
native/C# bindings and shader layouts must match.

Target machines need .NET 8 x64, a Vulkan 1.3-capable driver with the engine's
bindless/dynamic-rendering features and an X11 or Wayland desktop. For default UI
text install DejaVu Sans, or ship a custom font asset. File paths on Linux are
case-sensitive: scene, material and script asset paths must match exact casing.

Current validation: Windows editor/player and CPU runtime regression tests pass.
This Windows workspace has no installed Linux/WSL distribution, so native Linux
compilation and execution remain to be verified in a Linux environment. The
editor reports a missing runtime explicitly; it never exports a renamed Windows
executable as a Linux player.
