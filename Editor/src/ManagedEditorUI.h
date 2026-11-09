#pragma once
#include "Scene/Scene.h"
namespace Stardust {
void DrawManagedWindows();
bool DrawManagedInspector(Scene& scene,EntityID entity,ScriptSlot& script,size_t slot,bool live);
}
