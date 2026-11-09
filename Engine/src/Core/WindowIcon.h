#pragma once
#include <filesystem>

struct GLFWwindow;
namespace Stardust::WindowIcon {
// PNG/JPEG/TGA/BMP source; aspect ratio and transparency are preserved.
bool Apply(GLFWwindow* window, const std::filesystem::path& image);
bool Embed(const std::filesystem::path& executable, const std::filesystem::path& image);
void ApplyEmbedded(GLFWwindow* window);
}
