#define NOMINMAX
#include <windows.h>

#include "EventText.h"
#include "GameCall.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace EventText {
    namespace {
        // 0x57664C
        constexpr uint8_t ShowPattern[] = {
            0x55, 0x8B, 0xEC, 0x6A, 0x00, 0x6A, 0x00, 0x53, 0x56, 0x8B, 0xF1, 0x8B, 0xDA, 0x33, 0xD2, 0x55, 0x68,
            0, 0, 0, 0,
            0x64, 0xFF, 0x32, 0x64, 0x89, 0x22, 0x8B, 0xD0, 0x83, 0xFA, 0x09, 0x0F, 0x87,
        };
        constexpr char ShowMask[] = "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxx";
        static_assert(sizeof(ShowPattern) == sizeof(ShowMask) - 1);
        constexpr uint32_t PlainStyle = 0;

        std::vector<char> MakeAnsiString(const wchar_t* Text) {
            const int Length = WideCharToMultiByte(CP_THREAD_ACP, 0, Text, -1, nullptr, 0, nullptr, nullptr) - 1;
            std::vector<char> Buffer(8 + std::max(Length, 0) + 1, '\0');
            const int32_t Header[2] = {-1, std::max(Length, 0)};
            std::memcpy(Buffer.data(), Header, sizeof(Header));
            if (Length > 0) WideCharToMultiByte(CP_THREAD_ACP, 0, Text, -1, Buffer.data() + 8, Length + 1, nullptr, nullptr);
            return Buffer;
        }
    }

    bool Show(const wchar_t* Text) {
        static const uintptr_t Function = GameCall::Find(ShowPattern, ShowMask);
        if (!Function || !Text || !*Text) return false;
        static std::vector<char> Current;
        Current = MakeAnsiString(Text);
        uint8_t Result = 0;
        GameCall::Call(Function, PlainStyle, reinterpret_cast<uint32_t>(Current.data() + 8), reinterpret_cast<uint32_t>(&Result));
        return true;
    }
}
