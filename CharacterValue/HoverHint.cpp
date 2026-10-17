#define NOMINMAX
#include <windows.h>

#include "HoverHint.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TNTIconWidget.h"
#include "WidgetKit.h"
#include "Memory.h"

#include <cstring>

namespace HoverHint {
    namespace {
        using Memory::Read;

        constexpr char HintClass[] = "TNTHintWidget";
        constexpr size_t HintSourceIcon = 0x90;
        constexpr size_t HintLineCount = 0x72;
        constexpr size_t HintLines = 0x8C;
        constexpr size_t LineStride = 0x10;
        constexpr uint16_t MaxLines = 64;
        constexpr int16_t LineHeight = 16;
        constexpr wchar_t LineText[] = L"Ctrl+Shift+F: search in NosBazaar";
        constexpr uint8_t LineFont = 1;
        constexpr int16_t SidePadding = 8;

        const ModHost* Host = nullptr;
        TLBSWidget* Hint = nullptr;
        TEWLabel* Line = nullptr;
        int16_t ExtendedBottom = INT16_MIN;
        int TextWidth = -1;
        uintptr_t Hovered = 0;

        const TNTIconWidget* InventoryIconOf(const TLBSWidget* Widget, const TLBSWidget* Inventory) {
            uintptr_t Icon = 0;
            if (!Read(reinterpret_cast<uintptr_t>(Widget) + HintSourceIcon, Icon) || !Icon) return nullptr;
            TLBSWidget* Parent = nullptr;
            IconProperties* Image = nullptr;
            if (!Read(Icon + 0xB0, Image) || !Image) return nullptr;
            if (!Read(Icon + 0x08, Parent) || Parent != Inventory) return nullptr;
            return reinterpret_cast<const TNTIconWidget*>(Icon);
        }

        Memory::RootLookup Hints(HintClass);

        TLBSWidget* FindHint(const TLBSWidget* Root, const TLBSWidget* Inventory) {
            for (TLBSWidget* Hint : Hints.All(Root)) {
                if (Hint->isVisible && InventoryIconOf(Hint, Inventory)) return Hint;
            }
            return nullptr;
        }

        void Show(TLBSWidget* Widget, const bool Visible) {
            if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
        }

        void Detach(TLBSWidget* Widget) {
            if (!Widget) return;
            Widget->isVisible = false;
            if (TLBSWidget* Parent = Widget->parent; Parent && Parent->childrenList &&
                Parent->childrenList->index_of(Widget) >= 0) {
                Parent->childrenList->remove(Widget);
            }
        }

        bool AttachLine(TLBSWidget* To) {
            Detach(Line);
            if (!Line) Line = Widget::Create<TEWLabel>(Host);
            if (!Line || !To->childrenList) return false;
            Line->fontStyle = LineFont;
            Line->textColor = Color(255, 200, 200, 200);
            Line->shadowColor = Color(0xAF, 0, 0, 0);
            Line->textAlignment = 3;
            Line->isInteractable = false;
            Line->SetText(LineText);
            Line->parent = To;
            To->childrenList->push_back(Line);
            Hint = To;
            ExtendedBottom = INT16_MIN;
            return true;
        }
    }

    void Startup(const ModHost* ModHost) {
        Host = ModHost;
    }

    void Shutdown() {
        if (Hint && ExtendedBottom != INT16_MIN && Hint->rect.bottom == ExtendedBottom) {
            Hint->rect.bottom = static_cast<int16_t>(Hint->rect.bottom - LineHeight);
        }
        Detach(Line);
        Line = nullptr;
        Hint = nullptr;
        Hovered = 0;
        Host = nullptr;
    }

    void EarlyTick(TLBSWidget* Root, TLBSWidget* Inventory) {
        Hovered = 0;
        if (!Host) return;
        TLBSWidget* Found = Inventory && Inventory->isVisible ? FindHint(Root, Inventory) : nullptr;
        if (!Found) {
            Show(Line, false);
            return;
        }
        if (Found != Hint && !AttachLine(Found)) return;

        Hovered = reinterpret_cast<uintptr_t>(InventoryIconOf(Found, Inventory)->image);

        if (Found->rect.bottom != ExtendedBottom) {
            if (TextWidth < 0) TextWidth = WidgetKit::MeasureText(LineFont, LineText); // 0x47FBC0
            const int16_t Needed = static_cast<int16_t>(TextWidth + 2 * SidePadding);
            if (TextWidth > 0 && Found->rect.right - Found->rect.left < Needed) {
                const int16_t Added = static_cast<int16_t>(Needed - (Found->rect.right - Found->rect.left));
                uint16_t Count = 0;
                uintptr_t Lines = 0;
                const auto Base = reinterpret_cast<uintptr_t>(Found);
                uint8_t Probe = 0;
                if (Read(Base + HintLineCount, Count) && Read(Base + HintLines, Lines) && Lines && Count <= MaxLines && Count &&
                    Read(Lines + Count * LineStride - 1, Probe)) {
                    for (uint16_t i = 0; i < Count; i++) *reinterpret_cast<uint16_t*>(Lines + i * LineStride) += Added / 2;
                }
                Found->rect.right = static_cast<int16_t>(Found->rect.left + Needed);
                if (Root && Found->rect.right > Root->rect.right) {
                    const int16_t Shift = static_cast<int16_t>(Found->rect.right - Root->rect.right);
                    Found->rect.left = static_cast<int16_t>(Found->rect.left - Shift);
                    Found->rect.right = static_cast<int16_t>(Found->rect.right - Shift);
                }
            }
            const int16_t Height = static_cast<int16_t>(Found->rect.bottom - Found->rect.top);
            const int16_t Width = static_cast<int16_t>(Found->rect.right - Found->rect.left);
            Line->rect = {0, static_cast<int16_t>(Height - 1), Width, static_cast<int16_t>(Height + LineHeight)};
            Line->pxPerLine = Width;
            ExtendedBottom = static_cast<int16_t>(Found->rect.bottom + LineHeight);
            Found->rect.bottom = ExtendedBottom;
        }
        Show(Line, true);
    }

    uintptr_t HoveredItem() {
        return Hovered;
    }
}
