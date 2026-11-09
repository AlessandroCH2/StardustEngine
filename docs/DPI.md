# Windows DPI scaling

Editor and player request Per-Monitor V2 awareness before GLFW creates a window.
The ImGui interface additionally reads the window's content scale at startup and
between frames. Fonts (including CJK fallbacks) are rasterized at that scale;
style metrics are regenerated from the unscaled theme, avoiding cumulative
rounding when moving between monitors. The Vulkan font texture is replaced only
after pending GPU work completes, without invalidating other UI texture handles.

Toolbar heights and the Hub/Build Settings dimensions follow UI scale. Game
Canvas resolution/scaling continues to use its own Canvas settings.

Manual display checks: start at 100%, 125%, 150% and 200%; move the editor between
monitors with different scales; verify readable sharp text, unclipped toolbar,
Hub and Build Settings, and matching mouse positions in Game/Scene views. Moving
back to 100% must restore the original spacing. These mixed-monitor checks require
the corresponding physical display setup; runtime regression tests do not cover
that setup.
