#pragma once
#include <filesystem>
#include <string>
#include <stdexcept>

namespace AssetPath {

inline std::filesystem::path g_exeDir;

// Call once at the top of main() with argv[0]
inline void init(const char* argv0) {
    namespace fs = std::filesystem;
    try {
        // On Linux/Windows, canonical resolves symlinks and makes absolute
        g_exeDir = fs::canonical(fs::path(argv0).parent_path());
    } catch (...) {
        // Fallback: use current working directory
        g_exeDir = fs::current_path();
    }
}

// Returns absolute path string for a file relative to the executable directory.
// Usage: assetPath("terrain_vert.spv")  ->  "/path/to/build/client/terrain_vert.spv"
inline std::string get(const char* relative) {
    return (g_exeDir / relative).string();
}

} // namespace AssetPath
