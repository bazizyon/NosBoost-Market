#define NOMINMAX
#include <windows.h>

#include "SearchHotkey.h"
#include "EventText.h"
#include "ItemCatalog.h"
#include "TLBSWidget.h"
#include "TEWEditWidget.h"
#include "WidgetKit.h"
#include "Memory.h"

#include <cstring>
#include <string>

namespace SearchHotkey {
    namespace {
        using Memory::Read;
        using Memory::IsClass;

        constexpr char BazaarClass[] = "TNTConsignmentWidget";
        constexpr size_t BazaarNameInput = 0x358;
        constexpr size_t ItemDefinition = 0x08;
        constexpr size_t DefinitionVnum = 0x30;

        bool KeyWasDown = false;
        bool BazaarWasOpen = false;
        std::wstring Pending;
        Memory::RootLookup Bazaars(BazaarClass);

        std::wstring NameOf(const uintptr_t Item) {
            uintptr_t Definition = 0;
            uint32_t Vnum = 0;
            if (!Read(Item + ItemDefinition, Definition) || !Read(Definition + DefinitionVnum, Vnum)) return {};
            const ItemCatalog::Item* Entry = ItemCatalog::Find(static_cast<int>(Vnum));
            return Entry ? Entry->Name : std::wstring{};
        }

        bool GameHasFocus() {
            DWORD Pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &Pid);
            return Pid == GetCurrentProcessId();
        }

        bool HotkeyPressed() {
            const bool Down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                              (GetAsyncKeyState('F') & 0x8000);
            const bool Pressed = Down && !KeyWasDown;
            KeyWasDown = Down;
            return Pressed && GameHasFocus();
        }

        bool FillSearchBox(TLBSWidget* Bazaar, const std::wstring& Name) {
            TEWEditWidget* Edit = nullptr;
            if (!Read(reinterpret_cast<uintptr_t>(Bazaar) + BazaarNameInput, Edit) || !Edit || !IsClass(Edit, TEWEditWidget::ClassName)) return false;
            Edit->SetText(Name.c_str());
            Edit->caretIndex = static_cast<int32_t>(Name.size());
            const int Width = WidgetKit::MeasureText(static_cast<uint8_t>(Edit->font), Name.c_str()); // 0x47FBC0
            const int Visible = Edit->rect.right - Edit->rect.left;
            Edit->scrollX = Width > Visible ? Width - Visible : 0;
            Edit->caretX = Width > 0 ? Width - Edit->scrollX : 0;
            return true;
        }
    }

    void EarlyTick(TLBSWidget* Root, const uintptr_t HoveredItem) {
        TLBSWidget* Bazaar = Bazaars.First(Root);
        const bool BazaarOpen = Bazaar && Bazaar->isVisible;

        if (HotkeyPressed() && HoveredItem) {
            const std::wstring Name = NameOf(HoveredItem);
            if (!Name.empty()) {
                if (BazaarOpen) {
                    FillSearchBox(Bazaar, Name);
                    Pending.clear();
                } else {
                    Pending = Name;
                    EventText::Show((Name + L" will be filled in when you open the NosBazaar.").c_str());
                }
            }
        }

        if (BazaarOpen && !BazaarWasOpen && !Pending.empty()) {
            FillSearchBox(Bazaar, Pending);
            Pending.clear();
        }
        BazaarWasOpen = BazaarOpen;
    }

    void Reset() {
        KeyWasDown = false;
        BazaarWasOpen = false;
        Pending.clear();
    }
}
