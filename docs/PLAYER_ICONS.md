# Player and editor icons

In **File → Build Settings → Player → Game Icon**, select an image from the
project or drag it from the Project panel. PNG, JPEG, BMP and TGA are supported;
a square PNG with transparency is recommended. Clear the field to use Stardust's
default icon.

Build or Build And Run applies the icon to the exported executable (Windows
Explorer and shortcuts) and the running game's window/taskbar. Six embedded
sizes are generated: 16, 32, 48, 64, 128 and 256 pixels. Non-square images retain
their aspect ratio with transparent padding. The setting is saved as
`player.icon`, relative to Assets, and the image is included in the main pak,
even if its folder is otherwise optional. Runtime reads it directly from the pak.

The cook command also updates the icon when the named player executable is
already present in the output folder. Copy the player executable before cooking,
as the editor's export flow does. Existing builds must be rebuilt to pick up
changes. Windows Explorer may cache old icons for an executable path.

Stardust Editor and the default player embed `Engine/Resources/EngineIcon.ico`,
generated with the root `engineIcon.png` from `Engine/Resources/EngineIcon.svg` by `python Tools/MakeEngineIcon.py`. The icon is available without
loose branding files beside the executable. To replace the engine branding,
edit the SVG and the matching shapes in the script, run it, and rebuild editor/player.
