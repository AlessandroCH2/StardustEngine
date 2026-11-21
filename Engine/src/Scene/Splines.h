#pragma once
// Spline curves (SplineComponent) and Line Renderer geometry
#include "Scene/GameplayComponents.h"
#include <glm/glm.hpp>
#include <vector>

namespace Stardust {
class Scene;
struct Vertex;
struct RenderCamera;
}

namespace Stardust::Splines {
// t: 0..1 along the whole curve, by parameter (not distance). Local space of the spline object.
glm::vec3 Position(const SplineComponent& c, float t);
glm::vec3 Tangent(const SplineComponent& c, float t);   // normalized
glm::vec3 Up(const SplineComponent& c, float t);        // with the knots' roll
float Length(const SplineComponent& c);
// Distance along the curve (metres) to parameter t
float ParameterAtDistance(const SplineComponent& c, float distance);
// Closest point to p (local space); returns t
float Nearest(const SplineComponent& c, const glm::vec3& p, glm::vec3* point = nullptr);
// Polyline through the curve (local space) for drawing
void Sample(const SplineComponent& c, std::vector<glm::vec3>& out, int perSegment = 16);
void AutoTangents(SplineComponent& c);
// Play mode: moves the followers
void Update(Scene& scene, float dt);
}

namespace Stardust::LineRenderers {
u32 BuildVertices(const LineRendererComponent& c, const glm::mat4& world, const RenderCamera& camera,
                  std::vector<Vertex>& out, glm::vec3& boundsMin, glm::vec3& boundsMax);
}
