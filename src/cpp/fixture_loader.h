#pragma once

#include "cpu_flp2.h"

#include <filesystem>
#include <string>

bool LoadFlp2Fixture(const std::filesystem::path& path, Flp2Fixture& fx, std::string& err);

// Search the given path, then benchmarks/fixtures/ and fixtures/ under
// common roots (cwd, exe-adjacent, repo walk). Empty hint → tiny_flp2.json.
std::filesystem::path ResolveFlp2Fixture(const std::filesystem::path& hint);
