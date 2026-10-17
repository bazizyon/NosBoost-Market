#define NOMINMAX
#include <windows.h>

#include "ModContract.h"
#include "Features.h"
#include "ItemEditor.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWEditWidget.h"
#include "TEWButtonWidget.h"
#include "TEWCustomPanelWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TNTConsignmentWidget.h"
#include "TNTIconWidget.h"

#include <filesystem>
#include <iterator>

namespace {
    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWButtonWidget::ClassName, TEWButtonWidget::Version, TEWButtonWidget::ExpectedSize},
        {TEWEditWidget::ClassName, TEWEditWidget::Version, TEWEditWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTConsignmentWidget::ClassName, TNTConsignmentWidget::Version, TNTConsignmentWidget::ExpectedSize},
        {TNTIconWidget::ClassName, TNTIconWidget::Version, TNTIconWidget::ExpectedSize},
    };

    const ModHost* Host = nullptr;

    std::filesystem::path ModsDir() {
        wchar_t Exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, Exe, MAX_PATH);
        return std::filesystem::path(Exe).parent_path() / L"mods";
    }

    void MoveIfMissing(const std::filesystem::path& From, const std::filesystem::path& To) {
        std::error_code Error;
        if (std::filesystem::exists(From, Error) && !std::filesystem::exists(To, Error)) {
            std::filesystem::rename(From, To, Error);
        }
    }

    void MoveOldFiles() {
        const auto Mods = ModsDir();
        std::error_code Error;
        std::filesystem::create_directories(Mods / L"Market", Error);
        MoveIfMissing(Mods / L"BazaarFilter.ini", Mods / L"Market" / L"BazaarFilter.ini");
        MoveIfMissing(Mods / L"ItemValue", Mods / L"Market" / L"CharacterValue");
    }
}

extern "C" {
    __declspec(dllexport) const ModClassRequirement* ModGetRequirements(size_t* OutCount) {
        *OutCount = std::size(Requirements);
        return Requirements;
    }

    __declspec(dllexport) void ModStartup(ImGuiContext* Context, const ImGuiMemAllocFunc AllocFunc,
                                          const ImGuiMemFreeFunc FreeFunc, void* AllocUserData, const ModHost* ModHost) {
        ImGui::SetCurrentContext(Context);
        ImGui::SetAllocatorFunctions(AllocFunc, FreeFunc, AllocUserData);
        Host = ModHost;
        MoveOldFiles();
        BazaarFilter::Startup(Host);
        CharacterValue::Startup(Host);
    }

    __declspec(dllexport) void ModShutdown() {
        CharacterValue::Shutdown();
        BazaarFilter::Shutdown();
        Host = nullptr;
    }

    __declspec(dllexport) void ModEarlyTick(const TLBSWidget* RootWidget, const TickContext tickContext) {
        TLBSWidget* Root = const_cast<TLBSWidget*>(RootWidget);
        BazaarFilter::EarlyTick(Root, tickContext);
        CharacterValue::EarlyTick(Root, tickContext);
    }

    __declspec(dllexport) void ModTick(const TLBSWidget*, TickContext) {
        CharacterValue::Tick();
    }

    __declspec(dllexport) void ModToggleMainWindow() {
        ItemEditor::Toggle();
    }
}
