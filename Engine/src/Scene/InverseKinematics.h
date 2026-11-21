#pragma once
// IK Constraint components: applied to the bones after the Animator, before physics and rendering.
#include "Core/Base.h"

namespace Stardust { class Scene; }
namespace Stardust::InverseKinematics {
void Update(Scene& scene);
}
