#include "ResourcePath.hpp"

#include <vector>

#ifdef __APPLE__

#include <mach-o/dyld.h>

std::filesystem::path getResourceDirectory() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::vector<char> buffer(size);
    _NSGetExecutablePath(buffer.data(), &size);

    std::filesystem::path executablePath(buffer.data());

    // Executable: Pong.app/Contents/MacOS/pong
    // Resources:  Pong.app/Contents/Resources
    return executablePath.parent_path().parent_path() / "Resources";
}

#elif defined(_WIN32)

#define NOMINMAX
#include <windows.h>

std::filesystem::path getResourceDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);

    DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );

    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    std::filesystem::path executablePath(
        std::wstring(buffer.data(), length)
    );

    // Executable: Pong/pong.exe
    // Resources:  Pong/assets
    return executablePath.parent_path();
}

#else

#error "ResourcePath is not implemented for this platform yet"

#endif