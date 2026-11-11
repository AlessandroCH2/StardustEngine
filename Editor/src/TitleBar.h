#pragma once
namespace Stardust::TitleBar {

// Replaces the native title bar with the editor's own while keeping resizing, snapping and the window shadow.
void Install(void* nativeWindow);
// Screen area of the menu bar that drags the window (everything else in the bar stays clickable).
void SetDragArea(float height, float startX, float endX);
// A clickable part inside the drag area (the project title)
void SetDragHole(float startX, float endX);

} // namespace Stardust::TitleBar
