#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef WOTM_NATIVE_DIR
#define WOTM_NATIVE_DIR "."
#endif

namespace wotm::assets
{
inline std::string file(const char *name)
{
    if (const char *dir = std::getenv("WOTM_ASSET_DIR"); dir && *dir)
        return (std::filesystem::path(dir) / name).string();
    return (std::filesystem::path(WOTM_NATIVE_DIR) / "assets" / name).string();
}
} // namespace wotm::assets
