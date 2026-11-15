#pragma once
#include "Project/Project.h"
#include <set>
#include <functional>
namespace Stardust {
struct CookReport {std::set<std::string> mainAssets;std::vector<std::string> archives;};
class BuildCooker {public:static bool Cook(const std::filesystem::path& project,const std::filesystem::path& output,const ProjectConfig& config,CookReport* report=nullptr,const std::function<void(float,const std::string&)>& progress={});};
}
