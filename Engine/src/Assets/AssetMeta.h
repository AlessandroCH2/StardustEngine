#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>

namespace Stardust::AssetMeta {

// Every asset can have a hidden "<file>.meta" beside it, like Unity: an id plus one section per importer
// ("texture", "model"). It replaces the old "<file>.import.json" and "<file>.rig.json" side files.
std::filesystem::path PathFor(const std::string& asset);

// The section's settings, or an empty object. Old side files are converted the first time they are read.
nlohmann::json Read(const std::string& asset, const char* section);
bool Write(const std::string& asset, const char* section, const nlohmann::json& settings);

// Converts every old side file below the folder; returns how many assets changed
int MigrateFolder(const std::filesystem::path& folder);

// Gives a copied asset its own id, so two assets never share one
void NewIdentity(const std::filesystem::path& metaFile);

// True for files the Project window should never list
bool IsSideFile(const std::filesystem::path& file);

} // namespace Stardust::AssetMeta
