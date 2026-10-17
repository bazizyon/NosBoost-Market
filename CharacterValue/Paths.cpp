#define NOMINMAX
#include <windows.h>

#include "Paths.h"

namespace Paths {
    std::filesystem::path DataDir() {
        static std::filesystem::path Dir;
        if (Dir.empty()) {
            wchar_t Exe[MAX_PATH]{};
            GetModuleFileNameW(nullptr, Exe, MAX_PATH);
            Dir = std::filesystem::path(Exe).parent_path() / L"mods" / L"Market" / L"CharacterValue";
        }
        std::error_code Error;
        std::filesystem::create_directories(Dir, Error);
        return Dir;
    }
}
