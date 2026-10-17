#define NOMINMAX
#include <windows.h>

#include "InventoryOverlay.h"
#include "ItemCatalog.h"
#include "ValueRules.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWCustomPanelWidget.h"
#include "TNTIconWidget.h"
#include "TEWLabels.h"
#include "GameCall.h"
#include "Memory.h"
#include "ItemCatalog.h"
#include "WidgetKit.h"

#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include <optional>
#include <cstring>
#include <string>

namespace InventoryOverlay {
    namespace {
        using Memory::Read;
        using Memory::IsClass;

        struct PanelSpec {
            const char* Class;
            size_t Slots;
            int SlotCount;
            size_t Lists;
            bool Tabbed;
            size_t TabField;
            int16_t TotalLeft, TotalRight, TotalTextY;
            const wchar_t* TopTitle;
        };

        constexpr int16_t ButtonSize = 22, ButtonX = 271 - 4 - ButtonSize, ButtonY = 5;
        constexpr PanelSpec Specs[] = {
            {"TNTCharacterInventoryInfoWidget", 0xF0, 60, 0x228, true, 0x220, 146, ButtonX - 6, ButtonY + 5, L"Most valuable in this tab"},
            {"TNTRaidInventoryInfoWidget", 0xD0, 28, 0x144, false, 0, 150, 297 - 6, 11, L"Most valuable in the raid inventory"},
        };
        constexpr size_t MainPanel = 0;
        constexpr int EquipmentTab = 0;

        constexpr size_t ItemDefinition = 0x08;
        constexpr size_t ItemAmount = 0x20;
        constexpr size_t DefinitionVnum = 0x30;

        constexpr ptrdiff_t VmtClassName = -0x2C;
        constexpr int GoldVnum = 1046;
        constexpr int16_t TextNudge = -1;

        constexpr int16_t IconInset = 2;
        constexpr wchar_t NoValue[] = L"---";

        const ModHost* Host = nullptr;

        struct Ranked {
            int Vnum = 0;
            int64_t Amount = 0;
            int64_t Value = 0;
        };

        struct Panel {
            TLBSWidget* Window = nullptr;
            std::vector<TEWLabel*> SlotLabels;
            std::vector<std::wstring> SlotTexts;
            std::vector<bool> SlotShown;
            TEWLabel* TotalLabel = nullptr;
            std::wstring TotalText;
            bool TotalShown = false;
            std::vector<Ranked> Top;
        };
        Panel Panels[std::size(Specs)];
        Memory::RootLookup PanelLookups[] = {Memory::RootLookup(Specs[0].Class), Memory::RootLookup(Specs[1].Class)};
        static_assert(std::size(PanelLookups) == std::size(Specs));
        Memory::RootLookup HintLookup("TNTHintWidget");
        bool ShownView = false;

        TEWCustomPanelWidget* ButtonFrame = nullptr;
        TNTIconWidget* ButtonIcon = nullptr;

        constexpr int TopCount = 10;
        constexpr int16_t TopLineHeight = 16;
        constexpr int16_t TopPadding = 8;
        constexpr char HintClass[] = "TNTHintWidget";
        // 0x74A39C
        constexpr uint8_t HintCreatePattern[] = {
            0x55, 0x8B, 0xEC, 0x83, 0xC4, 0xF4, 0x53, 0x56, 0x84, 0xD2, 0x74, 0x08, 0x83, 0xC4, 0xF0, 0xE8, 0, 0, 0, 0,
            0x88, 0x55, 0xFF, 0x8B, 0xD8, 0x33, 0xD2, 0x8B, 0xC3, 0xE8, 0, 0, 0, 0,
            0x33, 0xF6, 0x8B, 0xCB, 0xB2, 0x01, 0xA1, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x89, 0x84, 0xB3, 0x9C, 0x00, 0x00, 0x00,
        };
        constexpr char HintCreateMask[] = "xxxxxxxxxxxxxxxx????xxxxxxxxxx????xxxxxxx????x????xxxxxxx";
        // 0x4A2090
        constexpr uint8_t SetLineCountPattern[] = {
            0x53, 0x56, 0x57, 0x55, 0x8B, 0xF2, 0x8B, 0xD8, 0x66, 0x3B, 0x73, 0x72, 0x74, 0,
            0x0F, 0xB7, 0xFE, 0x57, 0x8D, 0x83, 0x8C, 0x00, 0x00, 0x00, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x8B, 0x15,
        };
        constexpr char SetLineCountMask[] = "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxx";
        // 0x4A206C
        constexpr uint8_t SetLineTextPattern[] = {
            0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0xF2, 0x8B, 0xD8, 0x0F, 0xB7, 0xC6, 0x03, 0xC0,
            0x8B, 0x93, 0x8C, 0x00, 0x00, 0x00, 0x8D, 0x44, 0xC2, 0x08, 0x8B, 0xD7, 0xE8,
        };
        constexpr char SetLineTextMask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxx";
        static_assert(sizeof(HintCreatePattern) == sizeof(HintCreateMask) - 1);
        static_assert(sizeof(SetLineCountPattern) == sizeof(SetLineCountMask) - 1);
        static_assert(sizeof(SetLineTextPattern) == sizeof(SetLineTextMask) - 1);
        uintptr_t SetLineCountFn = 0, SetLineTextFn = 0;
        constexpr size_t HintFollowsSource = 0x98;

        TEWLabels* TopHint = nullptr;
        TLBSWidget* TopRoot = nullptr;
        std::wstring TopKey;

        bool PriceView = false;
        bool MouseWasDown = false;

        TNTIconWidget* SlotIcon(const PanelSpec& Spec, const Panel& State, const int Index) {
            uintptr_t Icon = 0;
            Read(reinterpret_cast<uintptr_t>(State.Window) + Spec.Slots + Index * 4, Icon);
            return reinterpret_cast<TNTIconWidget*>(Icon);
        }

        int Tab(const PanelSpec& Spec, const Panel& State) {
            int32_t Value = -1;
            if (Spec.Tabbed) Read(reinterpret_cast<uintptr_t>(State.Window) + Spec.TabField, Value);
            return Value;
        }

        struct ItemStack {
            int Vnum = 0;
            int Amount = 0;
        };

        bool ReadItem(const uintptr_t Item, const int OnTab, ItemStack& Out) {
            uintptr_t Definition = 0;
            uint32_t Vnum = 0, Amount = 1;
            if (!Item || !Read(Item + ItemDefinition, Definition) || !Read(Definition + DefinitionVnum, Vnum)) return false;
            if (OnTab != EquipmentTab && !Read(Item + ItemAmount, Amount)) return false;
            Out = {static_cast<int>(Vnum), static_cast<int>(Amount)};
            return Vnum != 0 && Amount != 0;
        }

        std::optional<int64_t> StackValue(const ItemStack& Stack) {
            const auto Unit = ValueRules::UnitValue(Stack.Vnum);
            if (!Unit) return std::nullopt;
            return *Unit * Stack.Amount;
        }

        std::wstring Compact(const int64_t Value) {
            if (Value < 1000) return std::to_wstring(Value);
            constexpr struct { double Scale; wchar_t Suffix; } Units[] = {{1e3, L'k'}, {1e6, L'm'}, {1e9, L'b'}, {1e12, L't'}};
            for (size_t i = 0; i < std::size(Units); i++) {
                const double Scaled = static_cast<double>(Value) / Units[i].Scale;
                if (Scaled >= 999.5 && i + 1 < std::size(Units)) continue;
                const int Decimals = Units[i].Scale >= 1e9 ? 3 : Scaled >= 99.95 ? 0 : Scaled >= 9.995 ? 1 : 2;
                wchar_t Text[32];
                swprintf(Text, 32, L"%.*f%c", Decimals, Scaled, Units[i].Suffix);
                return Text;
            }
            return std::to_wstring(Value);
        }

        void Attach(TLBSWidget* Parent, TLBSWidget* Child) {
            if (!Parent || !Child || !Parent->childrenList) return;
            Child->parent = Parent;
            Parent->childrenList->push_back(Child);
        }

        void Detach(TLBSWidget* Widget) {
            if (!Widget) return;
            Widget->isVisible = false;
            if (TLBSWidget* Parent = Widget->parent; Parent && Parent->childrenList &&
                Parent->childrenList->index_of(Widget) >= 0) {
                Parent->childrenList->remove(Widget);
            }
        }

        void Show(TLBSWidget* Widget, const bool Visible) {
            if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
        }

        void Place(TLBSWidget* Widget, const int16_t X, const int16_t Y, const int16_t Width, const int16_t Height) {
            if (Widget) Widget->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + Height)};
        }

        TEWLabel* CreateLabel(TLBSWidget* Parent, const int16_t X, const int16_t TextY, const int16_t Width, const uint8_t Alignment) {
            TEWLabel* Label = Widget::Create<TEWLabel>(Host);
            if (!Label) return nullptr;
            Place(Label, X, static_cast<int16_t>(TextY + TextNudge), Width, 16);
            Label->textAlignment = Alignment;
            Label->pxPerLine = Width;
            Label->isInteractable = false;
            Label->textColor = Color(255, 255, 214, 92);
            Label->SetText(L"");
            Attach(Parent, Label);
            return Label;
        }

        TEWCustomPanelWidget* CreateFrame(const int16_t X, const int16_t Y, const int16_t Size) {
            constexpr int16_t Left = 499, Top = 21, Corner = 3, Middle = 6;
            constexpr int16_t MiddleX = Left + Corner, RightX = MiddleX + Middle;
            constexpr int16_t MiddleY = Top + Corner, BottomY = MiddleY + Middle;
            TEWCustomPanelWidget* Frame = Widget::Create<TEWCustomPanelWidget>(Host);
            if (!Frame) return nullptr;
            Place(Frame, X, Y, Size, Size);
            delete[] Frame->imageData.atlasFrames;
            Frame->imageData.frameCount = 9;
            Frame->imageData.imageName = 1593835585;
            Frame->imageData.imageWidth = 512;
            Frame->imageData.imageHeight = 512;
            Frame->imageData.atlasFrames = new AtlasFrame[9]{
                {MiddleX, MiddleY, Middle, Middle}, {Left, Top, Corner, Corner}, {MiddleX, Top, Middle, Corner},
                {RightX, Top, Corner, Corner}, {RightX, MiddleY, Corner, Middle}, {RightX, BottomY, Corner, Corner},
                {MiddleX, BottomY, Middle, Corner}, {Left, BottomY, Corner, Corner}, {Left, MiddleY, Corner, Middle},
            };
            const auto Inner = static_cast<uint16_t>(Size - 2 * Corner);
            Frame->nineSliceInfo = {Inner, Inner, static_cast<uint16_t>(Corner + Inner), static_cast<uint16_t>(Corner + Inner),
                                    Corner, Corner, Corner, Corner};
            Frame->sliceCount = 1;
            Frame->drawMode = 5;
            Frame->isInteractable = true;
            return Frame;
        }

        int16_t GoldIconId() {
            if (const ItemCatalog::Item* Gold = ItemCatalog::Find(GoldVnum)) return Gold->Icon;
            return 0;
        }

        TNTIconWidget* CreateIcon(TLBSWidget* Parent, const int16_t X, const int16_t Y, const int16_t Size, const int16_t ImageId) {
            TNTIconWidget* Icon = Widget::Create<TNTIconWidget>(Host);
            if (!Icon) return nullptr;
            auto* Raw = reinterpret_cast<uint8_t*>(Icon);
            Icon->keepTransparency = false;
            Icon->resized = true;
            *reinterpret_cast<float*>(Raw + 0x90) = 1.0f;
            Raw[0xAB] = 1;
            Raw[0xAC] = 1;
            Raw[0xAE] = 10;
            Raw[0xD2] = 1;
            Raw[0xD3] = 1;
            Icon->isInteractable = false;
            Icon->image->imageID = ImageId;
            Icon->image->type = 3;
            Place(Icon, X, Y, Size, Size);
            Attach(Parent, Icon);
            return Icon;
        }

        std::wstring ItemName(const int Vnum) {
            const ItemCatalog::Item* Item = ItemCatalog::Find(Vnum);
            return Item && !Item->Name.empty() ? Item->Name : L"#" + std::to_wstring(Vnum);
        }

        void SetEntry(const int Index, const int Row, const int16_t X, const int16_t Width, const std::wstring& Text,
                      const uint32_t TextColor) {
            const BSTR Copy = SysAllocString(Text.c_str());
            GameCall::Call(SetLineTextFn, reinterpret_cast<uint32_t>(TopHint), static_cast<uint32_t>(Index), reinterpret_cast<uint32_t>(Copy));
            SysFreeString(Copy);
            TextData& Line = TopHint->textDatas[Index];
            Line.xOffset = static_cast<uint16_t>(X);
            Line.yOffset = static_cast<uint16_t>(TopPadding / 2 + Row * TopLineHeight);
            Line.pxPerLine = static_cast<uint16_t>(Width);
            Line.isVisible = true;
            TextStyle& Style = TopHint->textStyles[Index];
            Style.textColor = TextColor;
            Style.textAlignment = 1;
        }

        uintptr_t HintClassRef(const TLBSWidget* Root) {
            const TLBSWidget* Hint = HintLookup.First(Root);
            uintptr_t ClassRef = 0;
            return Hint && Read(reinterpret_cast<uintptr_t>(Hint), ClassRef) ? ClassRef : 0;
        }

        bool CreateTopHint(TLBSWidget* Root) {
            static const uintptr_t Create = GameCall::Find(HintCreatePattern, HintCreateMask);
            if (!SetLineCountFn) SetLineCountFn = GameCall::Find(SetLineCountPattern, SetLineCountMask);
            if (!SetLineTextFn) SetLineTextFn = GameCall::Find(SetLineTextPattern, SetLineTextMask);
            if (!Create || !SetLineCountFn || !SetLineTextFn || !Root || !Root->childrenList) return false;
            const uintptr_t ClassRef = HintClassRef(Root);
            if (!ClassRef) return false;
            const uint32_t ChildrenBefore = Root->childrenList->count;
            TopHint = reinterpret_cast<TEWLabels*>(GameCall::Call(Create, static_cast<uint32_t>(ClassRef), 1, reinterpret_cast<uint32_t>(Root)));
            if (!TopHint) return false;
            TopHint->isVisible = false;
            TopHint->isInteractable = false;
            // +0x98
            *(reinterpret_cast<uint8_t*>(TopHint) + HintFollowsSource) = 0;
            if (Root->childrenList->count == ChildrenBefore || Root->childrenList->index_of(TopHint) < 0) {
                TopHint->parent = Root;
                Root->childrenList->push_back(TopHint);
            }
            TopRoot = Root;
            return true;
        }

        void FillTopHint(const size_t Index) {
            const PanelSpec& Spec = Specs[Index];
            const Panel& State = Panels[Index];
            std::wstring Key = std::to_wstring(Index) + L":" + std::to_wstring(Tab(Spec, State));
            for (const Ranked& Entry : State.Top) Key += L"|" + std::to_wstring(Entry.Vnum) + L":" + std::to_wstring(Entry.Value);
            if (Key == TopKey) return;
            TopKey = Key;

            constexpr uint32_t HeaderColor = 0xFFFFF0B4, PriceColor = 0xFFFFD65C, NameColor = 0xFFFFFFFF;
            constexpr int16_t ColumnGap = 10;
            const std::wstring Header = State.Top.empty() ? L"Nothing valued here yet" : Spec.TopTitle;
            std::vector<std::wstring> Prices, Names;
            int PriceWidth = 0, NameWidth = 0;
            for (const Ranked& Entry : State.Top) {
                Prices.push_back(Compact(Entry.Value));
                Names.push_back(ItemName(Entry.Vnum) + (Entry.Amount > 1 ? L" x" + std::to_wstring(Entry.Amount) : L""));
                PriceWidth = std::max(PriceWidth, WidgetKit::MeasureText(1, Prices.back().c_str()));
                NameWidth = std::max(NameWidth, WidgetKit::MeasureText(1, Names.back().c_str()));
            }
            const int RowsWidth = Prices.empty() ? 0 : PriceWidth + ColumnGap + NameWidth;
            const int Inner = std::max({120, WidgetKit::MeasureText(1, Header.c_str()), RowsWidth});
            const auto HintWidth = static_cast<int16_t>(Inner + 2 * TopPadding);

            const auto Count = static_cast<uint32_t>(1 + 2 * Prices.size());
            GameCall::Call(SetLineCountFn, reinterpret_cast<uint32_t>(TopHint), Count, 0);
            if (TopHint->lineCount != Count) return;
            SetEntry(0, 0, TopPadding, static_cast<int16_t>(Inner), Header, HeaderColor);
            const auto NameX = static_cast<int16_t>(TopPadding + PriceWidth + ColumnGap);
            for (size_t i = 0; i < Prices.size(); i++) {
                const int Row = static_cast<int>(i) + 1;
                const int Measured = std::max(0, WidgetKit::MeasureText(1, Prices[i].c_str()));
                const auto PriceX = static_cast<int16_t>(TopPadding + PriceWidth - Measured);
                SetEntry(Row * 2 - 1, Row, PriceX, static_cast<int16_t>(Measured + 2), Prices[i], PriceColor);
                SetEntry(Row * 2, Row, NameX, static_cast<int16_t>(NameWidth + 2), Names[i], NameColor);
            }
            TopHint->maxTextWidthPx = static_cast<uint16_t>(Inner);
            const auto Height = static_cast<int16_t>((1 + Prices.size()) * TopLineHeight + TopPadding);
            TopHint->rect.right = static_cast<int16_t>(TopHint->rect.left + HintWidth);
            TopHint->rect.bottom = static_cast<int16_t>(TopHint->rect.top + Height);
        }

        bool OverTotal(const Panel& State, const TickContext& Context) {
            if (!State.Window || !State.Window->isVisible || !State.TotalLabel) return false;
            const int32_t X = Context.mouseX - State.Window->rect.left;
            const int32_t Y = Context.mouseY - State.Window->rect.top;
            const Rect& Area = State.TotalLabel->rect;
            return X >= Area.left && X < Area.right && Y >= Area.top && Y < Area.top + 18;
        }

        void UpdateTopHint(const TickContext& Context) {
            if (!TopHint) return;
            size_t Hovered = std::size(Specs);
            if (PriceView) {
                for (size_t i = 0; i < std::size(Specs); i++) {
                    if (OverTotal(Panels[i], Context)) Hovered = i;
                }
            }
            if (Hovered == std::size(Specs)) {
                Show(TopHint, false);
                return;
            }
            FillTopHint(Hovered);
            const int16_t Width = static_cast<int16_t>(TopHint->rect.right - TopHint->rect.left);
            const int16_t Height = static_cast<int16_t>(TopHint->rect.bottom - TopHint->rect.top);
            auto Left = static_cast<int16_t>(Context.mouseX + 12);
            auto Top = static_cast<int16_t>(Context.mouseY + 20);
            if (TopRoot && Left + Width > TopRoot->rect.right) Left = static_cast<int16_t>(TopRoot->rect.right - Width);
            if (TopRoot && Top + Height > TopRoot->rect.bottom) Top = static_cast<int16_t>(Context.mouseY - Height - 4);
            if (TopHint->rect.left != Left || TopHint->rect.top != Top) TopHint->rect = {Left, Top, static_cast<int16_t>(Left + Width), static_cast<int16_t>(Top + Height)};
            if (!TopHint->isVisible) {
                Show(TopHint, true);
                TopHint->BubbleUp();
            }
        }

        void DestroyPanel(Panel& State) {
            for (TEWLabel* Label : State.SlotLabels) Detach(Label);
            Detach(State.TotalLabel);
            State = {};
        }

        void DestroyButton() {
            Detach(ButtonIcon);
            Detach(ButtonFrame);
            ButtonIcon = nullptr;
            ButtonFrame = nullptr;
        }

        void DestroyAll() {
            DestroyButton();
            for (Panel& State : Panels) DestroyPanel(State);
            Detach(TopHint);
            TopHint = nullptr;
            TopRoot = nullptr;
            TopKey.clear();
            ShownView = false;
        }

        void CreatePanel(const size_t Index, TLBSWidget* Window) {
            const PanelSpec& Spec = Specs[Index];
            Panel& State = Panels[Index];
            State.Window = Window;
            State.SlotLabels.assign(Spec.SlotCount, nullptr);
            State.SlotTexts.assign(Spec.SlotCount, L"");
            State.SlotShown.assign(Spec.SlotCount, false);
            for (int i = 0; i < Spec.SlotCount; i++) {
                const TNTIconWidget* Icon = SlotIcon(Spec, State, i);
                if (!Icon) continue;
                State.SlotLabels[i] = CreateLabel(Window, static_cast<int16_t>(Icon->rect.left + 2), static_cast<int16_t>(Icon->rect.top + 1),
                                                  static_cast<int16_t>(Icon->rect.right - Icon->rect.left - 2), 1);
                Show(State.SlotLabels[i], false);
            }
            State.TotalLabel = CreateLabel(Window, Spec.TotalLeft, Spec.TotalTextY, static_cast<int16_t>(Spec.TotalRight - Spec.TotalLeft), 2);
            Show(State.TotalLabel, false);
        }

        void CreateButton(TLBSWidget* Inventory) {
            ButtonFrame = CreateFrame(ButtonX, ButtonY, ButtonSize);
            Attach(Inventory, ButtonFrame);
            ButtonIcon = CreateIcon(ButtonFrame, IconInset, IconInset, ButtonSize - 2 * IconInset, GoldIconId());
        }

        bool ButtonClicked(const TickContext& Context) {
            const bool Down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            const bool Pressed = Down && !MouseWasDown;
            MouseWasDown = Down;
            const TLBSWidget* Inventory = Panels[MainPanel].Window;
            if (!Pressed || !ButtonFrame || !Inventory || !Inventory->isVisible) return false;

            DWORD Pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &Pid);
            if (Pid != GetCurrentProcessId()) return false;

            const int32_t X = Context.mouseX - Inventory->rect.left;
            const int32_t Y = Context.mouseY - Inventory->rect.top;
            const Rect& Area = ButtonFrame->rect;
            return X >= Area.left && X < Area.right && Y >= Area.top && Y < Area.bottom;
        }

        void SetText(TEWLabel* Label, std::wstring& Cached, const std::wstring& Text) {
            if (!Label || Cached == Text) return;
            Cached = Text;
            Label->SetText(Text.c_str());
        }

        void Refresh(const PanelSpec& Spec, Panel& State) {
            const int OnTab = Tab(Spec, State);
            for (int i = 0; i < Spec.SlotCount; i++) {
                TEWLabel* Label = State.SlotLabels[i];
                if (!Label) continue;
                const TNTIconWidget* Icon = SlotIcon(Spec, State, i);
                ItemStack Stack;
                const bool HasItem = PriceView && Icon && ReadItem(reinterpret_cast<uintptr_t>(Icon->image), OnTab, Stack);
                if (HasItem) {
                    const auto Value = StackValue(Stack);
                    SetText(Label, State.SlotTexts[i], Value ? Compact(*Value) : std::wstring(NoValue));
                }
                if (State.SlotShown[i] != HasItem) {
                    State.SlotShown[i] = HasItem;
                    Show(Label, HasItem);
                }
            }

            if (PriceView) {
                int64_t Total = 0;
                bool Any = false;
                std::map<int, Ranked> ByVnum;
                uintptr_t List = 0, Entries = 0;
                uint32_t Count = 0;
                const size_t ListField = Spec.Lists + (Spec.Tabbed ? static_cast<size_t>(std::max(OnTab, 0)) * 4 : 0);
                const bool KnownTab = !Spec.Tabbed || (OnTab >= 0 && OnTab <= 2);
                if (KnownTab && Read(reinterpret_cast<uintptr_t>(State.Window) + ListField, List) &&
                    List && Read(List + 4, Entries) && Read(List + 8, Count) && Count <= 512) {
                    for (uint32_t i = 0; i < Count; i++) {
                        uintptr_t Record = 0, Item = 0;
                        ItemStack Stack;
                        if (!Read(Entries + i * 4, Record) || !Read(Record, Item) || !ReadItem(Item, OnTab, Stack)) continue;
                        if (const auto Value = StackValue(Stack)) {
                            Total += *Value;
                            Any = true;
                            Ranked& Entry = ByVnum[Stack.Vnum];
                            Entry.Vnum = Stack.Vnum;
                            Entry.Amount += Stack.Amount;
                            Entry.Value += *Value;
                        }
                    }
                }
                SetText(State.TotalLabel, State.TotalText, L"Total " + (Any ? Compact(Total) : std::wstring(NoValue)));
                State.Top.clear();
                for (const auto& [Vnum, Entry] : ByVnum) State.Top.push_back(Entry);
                std::sort(State.Top.begin(), State.Top.end(), [](const Ranked& A, const Ranked& B) { return A.Value > B.Value; });
                if (State.Top.size() > TopCount) State.Top.resize(TopCount);
            }
            if (State.TotalShown != PriceView) {
                State.TotalShown = PriceView;
                Show(State.TotalLabel, PriceView);
            }
        }
    }

    void Startup(const ModHost* ModHost) {
        Host = ModHost;
    }

    void Shutdown() {
        DestroyAll();
        Host = nullptr;
    }

    void EarlyTick(TLBSWidget* Root, const TickContext& Context) {
        if (!Host || !Context.isPlayerLoaded || !GoldIconId()) return;
        if (!TopHint && Root) CreateTopHint(Root);

        for (size_t i = 0; i < std::size(Specs); i++) {
            TLBSWidget* Found = PanelLookups[i].First(Root);
            if (!Found || Found == Panels[i].Window) continue;
            if (i == MainPanel) DestroyButton();
            DestroyPanel(Panels[i]);
            CreatePanel(i, Found);
            if (i == MainPanel) CreateButton(Found);
        }

        if (ButtonClicked(Context)) PriceView = !PriceView;
        if (PriceView || ShownView) {
            for (size_t i = 0; i < std::size(Specs); i++) {
                if (Panels[i].Window && (Panels[i].Window->isVisible || !PriceView)) Refresh(Specs[i], Panels[i]);
            }
            ShownView = PriceView;
        }
        if (PriceView || (TopHint && TopHint->isVisible)) UpdateTopHint(Context);
    }

    TLBSWidget* Window() {
        return Panels[MainPanel].Window;
    }
}
