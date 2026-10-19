#define NOMINMAX
#include <windows.h>

#include "ItemEditor.h"
#include "Features.h"
#include "ItemCatalog.h"
#include "ValueRules.h"
#include "WidgetKit.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWEditWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TNTIconWidget.h"

#include <algorithm>
#include <cwchar>
#include <optional>
#include <string>
#include <vector>

namespace ItemEditor {
    namespace {
        constexpr uint16_t WindowWidth = 320;
        constexpr int16_t Pad = 14;
        constexpr int16_t ContentWidth = WindowWidth - 2 * Pad;
        constexpr int16_t DropdownArrow = 17;
        constexpr int16_t TextNudge = -1;

        constexpr int16_t PageTabsTop = 38;
        constexpr int16_t PageShift = 32;
        constexpr int16_t PickerTop = 42;
        constexpr int16_t HeaderTop = 72;
        constexpr int16_t HeaderIcon = 32;
        constexpr int16_t ValueTop = 112;
        constexpr int16_t ValuedTop = 136;
        constexpr int16_t AmountTop = 160;
        constexpr int16_t FixedTop = 188;
        constexpr int16_t FieldLabelWidth = 80;

        constexpr int16_t TabsTop = 220;
        constexpr int16_t SectionTop = TabsTop + 30;
        constexpr int16_t SectionAddOffset = 20;
        constexpr int16_t SectionRowsOffset = 48;
        constexpr int16_t RowHeight = 22;
        constexpr int VisibleRows = 8;
        constexpr int16_t PagerTop = SectionTop + SectionRowsOffset + VisibleRows * RowHeight + 2;
        constexpr int16_t RandomTop = PagerTop + 24;
        constexpr int16_t StatusTop = RandomTop + 26;
        constexpr int16_t TableTop = StatusTop + 26;
        constexpr uint16_t WindowHeight = TableTop + 32;

        constexpr int16_t CountWidth = 40;
        constexpr int16_t IconSize = 18;
        constexpr int16_t RemoveSize = 20;
        constexpr int16_t RemoveX = WindowWidth - Pad - RemoveSize;
        constexpr int16_t NameX = Pad + IconSize + 6;
        constexpr int16_t NameWidth = RemoveX - 4 - NameX;

        const Color PlainColor(255, 255, 255, 255);
        const Color HintColor(255, 190, 190, 190);
        const Color ValueColor(255, 255, 214, 92);
        const Color BadColor(255, 255, 110, 90);
        const Color GoodColor(255, 140, 220, 120);

        enum class LinkKind { ComesFrom, TurnsInto };

        struct LinkSection;
        struct LinkRow {
            TNTIconWidget* Icon = nullptr;
            TEWLabel* Name = nullptr;
            TEWGraphicButtonWidget* Remove = nullptr;
            LinkSection* Owner = nullptr;
            int Slot = 0;
        };

        struct LinkSection {
            LinkKind Kind = LinkKind::ComesFrom;
            int16_t Top = 0;
            const wchar_t* Title = L"";
            const wchar_t* Placeholder = L"";
            const wchar_t* EmptyText = L"";
            LinkRow Rows[VisibleRows];
            TEWLabel* Empty = nullptr;
            WidgetKit::EditBox Count{};
            WidgetKit::Dropdown* Picker = nullptr;
            TLBSWidget* Group = nullptr;
            TEWGraphicButtonWidget* Tab = nullptr;
            TEWLabel* TabCaption = nullptr;
            TEWGraphicButtonWidget* PrevPage = nullptr;
            TEWGraphicButtonWidget* NextPage = nullptr;
            TEWLabel* PageLabel = nullptr;
            int Scroll = 0;
        };

        const ModHost* Host = nullptr;
        TLBSWidget* AttachedRoot = nullptr;
        TEWCustomPanelWidget* Window = nullptr;
        TLBSWidget* Pages[2]{};
        WidgetKit::Tab PageTabs[2]{};
        int ActivePage = 1;
        bool FilterValue = false;
        TEWGraphicButtonWidget* FilterBox = nullptr;

        void CloseDropdowns();
        WidgetKit::Dropdown* ItemPicker = nullptr;
        TNTIconWidget* HeaderIconWidget = nullptr;
        TEWLabel* HeaderName = nullptr;
        TEWLabel* HeaderVnum = nullptr;
        TEWLabel* ValueLabel = nullptr;
        TLBSWidget* ValuedBox = nullptr;
        TLBSWidget* RandomBox = nullptr;
        TEWLabel* AmountLabel = nullptr;
        WidgetKit::EditBox AmountBox{};
        WidgetKit::EditBox FixedBox{};
        LinkSection Sections[2];
        int ActiveTab = 0;
        TEWLabel* Status = nullptr;
        TLBSWidget* TableBox = nullptr;
        TLBSWidget* Editors = nullptr;

        bool PendingToggle = false;
        int PendingSelect = 0;
        bool ValuedValue = true;
        bool RandomValue = false;
        bool TableValue = false;
        bool CatalogReady = false;
        std::vector<int> PickerVnums;

        int Selected = 0;
        std::wstring AppliedAmount, AppliedFixed;
        uint32_t ShownRevision = UINT32_MAX;

        void Attach(TLBSWidget* Parent, TLBSWidget* Child) {
            if (!Parent || !Child || !Parent->childrenList) return;
            Child->parent = Parent;
            Parent->childrenList->push_back(Child);
        }

        void Detach(TLBSWidget* Widget) {
            if (!Widget || !Widget->parent || !Widget->parent->childrenList) return;
            Widget->parent->childrenList->remove(Widget);
            Widget->parent = nullptr;
        }

        void Show(TLBSWidget* Widget, const bool Visible) {
            if (Widget && Widget->isVisible != Visible) Widget->isVisible = Visible;
        }

        void Place(TLBSWidget* Widget, const int16_t X, const int16_t Y, const int16_t Width, const int16_t Height) {
            if (Widget) Widget->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + Height)};
        }

        TEWLabel* AddLabel(TLBSWidget* Parent, const int16_t X, const int16_t TextY, const int16_t Width,
                           const uint8_t Alignment, const wchar_t* Text) {
            TEWLabel* Label = Widget::Create<TEWLabel>(Host);
            if (!Label) return nullptr;
            Place(Label, X, static_cast<int16_t>(TextY + TextNudge), Width, 30);
            Label->textAlignment = Alignment;
            Label->pxPerLine = Width;
            Label->SetText(Text);
            Attach(Parent, Label);
            return Label;
        }

        void SetLabel(TEWLabel* Label, const std::wstring& Text, const Color& TextColor) {
            if (!Label) return;
            Label->SetText(Text.c_str());
            Label->textColor = TextColor;
        }

        std::wstring Fit(const std::wstring& Text, const int Width) {
            if (WidgetKit::MeasureText(1, Text.c_str()) <= Width) return Text;
            std::wstring Cut = Text;
            while (!Cut.empty() && WidgetKit::MeasureText(1, (Cut + L"..").c_str()) > Width) Cut.pop_back();
            return Cut + L"..";
        }

        TEWGraphicButtonWidget* AddSpriteButton(TLBSWidget* Parent, std::initializer_list<AtlasFrame> Frames,
                                                const WidgetKit::ClickFn OnClick, void* Argument) {
            TEWGraphicButtonWidget* Button = Widget::Create<TEWGraphicButtonWidget>(Host);
            if (!Button) return nullptr;
            delete[] Button->imageData.atlasFrames;
            Button->imageData.imageName = 1593835585;
            Button->imageData.imageWidth = 512;
            Button->imageData.imageHeight = 512;
            Button->imageData.frameCount = static_cast<int16_t>(Frames.size());
            Button->imageData.atlasFrames = new AtlasFrame[Frames.size()];
            std::copy(Frames.begin(), Frames.end(), Button->imageData.atlasFrames);
            Button->drawMode = 0;
            WidgetKit::SetOnClick(Button, OnClick, Argument);
            Attach(Parent, Button);
            return Button;
        }

        TEWGraphicButtonWidget* AddBlueButton(TLBSWidget* Parent, const int16_t X, const int16_t Y, const int16_t Width,
                                              const wchar_t* Text, const WidgetKit::ClickFn OnClick, void* Argument,
                                              TEWLabel** CaptionOut) {
            TEWGraphicButtonWidget* Button = AddSpriteButton(Parent, {
                {419, 308, 5, 24}, {424, 308, 10, 24}, {409, 308, 10, 24},
                {445, 308, 5, 24}, {450, 308, 10, 24}, {435, 308, 10, 24},
                {471, 308, 5, 24}, {476, 308, 10, 24}, {461, 308, 10, 24},
            }, OnClick, Argument);
            if (!Button) return nullptr;
            constexpr int16_t Cap = 10;
            Button->nineSliceInfo = {static_cast<uint16_t>(Width - 2 * Cap), 25, static_cast<uint16_t>(Width - Cap), 25, Cap, 0, Cap, 0};
            Button->sliceCount = 3;
            Button->drawMode = 4;
            Button->rect = {X, Y, static_cast<int16_t>(X + Width), static_cast<int16_t>(Y + 25)};
            TEWLabel* Caption = AddLabel(Button, 0, 6, Width, 3, Text);
            if (Caption) Caption->shadowColor = Color(255, 1, 61, 255);
            if (CaptionOut) *CaptionOut = Caption;
            return Button;
        }

        std::wstring NameOf(const int Vnum) {
            const ItemCatalog::Item* Item = ItemCatalog::Find(Vnum);
            return Item && !Item->Name.empty() ? Item->Name : L"#" + std::to_wstring(Vnum);
        }

        int16_t IconOf(const int Vnum) {
            const ItemCatalog::Item* Item = ItemCatalog::Find(Vnum);
            return Item ? Item->Icon : static_cast<int16_t>(-1);
        }

        std::wstring Grouped(int64_t Value) {
            std::wstring Digits = std::to_wstring(Value);
            for (int i = static_cast<int>(Digits.size()) - 3; i > 0; i -= 3) Digits.insert(static_cast<size_t>(i), 1, L',');
            return Digits;
        }

        void SetStatus(const std::wstring& Text, const Color& TextColor) {
            SetLabel(Status, Fit(Text, ContentWidth), TextColor);
        }

        void SetBoxText(const WidgetKit::EditBox& Box, const std::wstring& Text, std::wstring* Applied) {
            if (!Box.Edit) return;
            Box.Edit->SetText(Text.c_str());
            Box.Edit->caretIndex = static_cast<int32_t>(Text.size());
            Box.Edit->caretX = std::max(0, WidgetKit::MeasureText(Box.Edit->font, Text.c_str()));
            Box.Edit->scrollX = 0;
            if (Applied) *Applied = Text;
        }

        void SetCheckbox(TLBSWidget* Box, const bool Checked) {
            if (Box && Box->childrenList && Box->childrenList->count > 0) {
                WidgetKit::SetChecked(reinterpret_cast<TEWGraphicButtonWidget*>(Box->childrenList->list[0]), Checked);
            }
        }

        std::optional<int64_t> ParseNumber(const std::wstring& Text) {
            std::wstring Digits;
            for (const wchar_t C : Text) {
                if (C >= L'0' && C <= L'9') Digits += C;
                else if (C != L',' && C != L'.' && C != L' ') return std::nullopt;
            }
            if (Digits.empty()) return 0;
            if (Digits.size() > 15) return std::nullopt;
            return std::wcstoll(Digits.c_str(), nullptr, 10);
        }

        std::vector<ValueRules::SourceLink> LinksOf(const LinkSection& Section) {
            if (!Selected) return {};
            return Section.Kind == LinkKind::ComesFrom ? ValueRules::SourcesOf(Selected) : ValueRules::ConversionsOf(Selected);
        }

        void RefreshValue() {
            if (!Selected) return;
            const auto Value = ValueRules::Find(Selected);
            std::wstring Text = L"Value: ";
            if (!ValueRules::IsValued(Selected)) Text += L"--- (switched off)";
            else if (!Value) Text += L"--- (search it in the bazaar)";
            else {
                Text += Grouped(Value->UnitValue);
                switch (Value->From) {
                    case ValueRules::Source::Bazaar: Text += L" (bazaar)"; break;
                    case ValueRules::Source::Fixed: Text += L" (fixed price)"; break;
                    case ValueRules::Source::External: Text += L" (scraper)"; break;
                    case ValueRules::Source::Manual: Text += L" (manual)"; break;
                    case ValueRules::Source::Via: Text += L" (via " + NameOf(Value->Via) + L")"; break;
                    case ValueRules::Source::Converts:
                        Text += Value->Via ? L" (as " + NameOf(Value->Via) + L")"
                                           : L" (average of " + std::to_wstring(Value->Options) + L")";
                        break;
                    default: break;
                }
            }
            SetLabel(ValueLabel, Fit(Text, ContentWidth), Value ? ValueColor : HintColor);
            SetLabel(AmountLabel, L"Buy amount", ValueRules::HasOwnBuyAmount(Selected) ? PlainColor : HintColor);
        }

        void RefreshSection(LinkSection& Section) {
            const auto Links = LinksOf(Section);
            const int Total = static_cast<int>(Links.size());
            Section.Scroll = std::clamp(Section.Scroll, 0, std::max(0, (Total - 1) / VisibleRows * VisibleRows));
            for (int i = 0; i < VisibleRows; i++) {
                LinkRow& Row = Section.Rows[i];
                const int Index = Section.Scroll + i;
                const bool Visible = Index < Total;
                Show(Row.Name, Visible);
                Show(Row.Remove, Visible);
                if (!Visible) {
                    WidgetKit::SetItemIcon(Row.Icon, -1);
                    continue;
                }
                const auto& Link = Links[Index];
                WidgetKit::SetItemIcon(Row.Icon, IconOf(Link.Vnum));
                std::wstring Text;
                const auto Value = ValueRules::Find(Link.Vnum);
                if (Section.Kind == LinkKind::ComesFrom) {
                    Text = (Link.Count > 1 ? std::to_wstring(Link.Count) + L" x " : L"") + NameOf(Link.Vnum);
                    if (Value) Text += L"  (" + Grouped(Value->UnitValue * Link.Count) + L")";
                } else {
                    Text = NameOf(Link.Vnum) + (Link.Count > 1 ? L" for " + std::to_wstring(Link.Count) : L"");
                    if (Value) Text += L"  (" + Grouped(Value->UnitValue / Link.Count) + (Link.Count > 1 ? L" each)" : L")");
                }
                SetLabel(Row.Name, Fit(Text, NameWidth), Value ? PlainColor : HintColor);
            }
            Show(Section.Empty, Links.empty());
            const bool Paged = Total > VisibleRows;
            Show(Section.PrevPage, Paged && Section.Scroll > 0);
            Show(Section.NextPage, Paged && Section.Scroll + VisibleRows < Total);
            Show(Section.PageLabel, Paged);
            if (Paged && Section.PageLabel) {
                Section.PageLabel->SetText((std::to_wstring(Section.Scroll + 1) + L"-" + std::to_wstring(std::min(Total, Section.Scroll + VisibleRows))
                                            + L" / " + std::to_wstring(Total)).c_str());
            }
        }

        void ShowTab(const int Index) {
            ActiveTab = Index;
            for (int i = 0; i < 2; i++) {
                LinkSection& Section = Sections[i];
                const bool Active = i == Index;
                Show(Section.Group, Active);
                if (Section.Picker) {
                    if (!Active) Section.Picker->SetOpen(false);
                    Show(Section.Picker->Widget(), Active && Selected != 0);
                }
                if (Section.TabCaption) Section.TabCaption->textColor = Active ? Color(255, 255, 240, 180) : Color(255, 140, 150, 170);
            }
        }

        void RefreshAll() {
            RefreshValue();
            for (LinkSection& Section : Sections) RefreshSection(Section);
        }

        void SelectItem(const int Vnum) {
            Selected = Vnum;
            Show(Editors, Vnum != 0);
            for (LinkSection& Section : Sections) Section.Scroll = 0;
            ShowTab(ActiveTab);
            if (!Vnum) return;
            WidgetKit::SetItemIcon(HeaderIconWidget, IconOf(Vnum));
            SetLabel(HeaderName, Fit(NameOf(Vnum), ContentWidth - HeaderIcon - 8), PlainColor);
            SetLabel(HeaderVnum, L"vnum " + std::to_wstring(Vnum), HintColor);
            ValuedValue = ValueRules::IsValued(Vnum);
            SetCheckbox(ValuedBox, ValuedValue);
            RandomValue = ValueRules::IsRandomConversion(Vnum);
            SetCheckbox(RandomBox, RandomValue);
            SetBoxText(AmountBox, ValueRules::HasOwnBuyAmount(Vnum) ? std::to_wstring(ValueRules::BuyAmount(Vnum)) : L"", &AppliedAmount);
            const int64_t Fixed = ValueRules::FixedPrice(Vnum);
            SetBoxText(FixedBox, Fixed > 0 ? std::to_wstring(Fixed) : L"", &AppliedFixed);
            RefreshAll();
            ShownRevision = ValueRules::Revision();
            SetStatus(L"Empty buy amount = default (" + std::to_wstring(ValueRules::BuyAmount(0)) + L"); empty fixed price = none.", HintColor);
        }

        void __cdecl OnItemPicked(void*, const int Index) {
            if (Index < 0 || Index >= static_cast<int>(PickerVnums.size())) return;
            SelectItem(PickerVnums[Index]);
        }

        void __cdecl OnLinkPicked(void* Argument, const int Index) {
            auto* Section = static_cast<LinkSection*>(Argument);
            if (Section->Picker) Section->Picker->SetSelected(-1);
            if (!Selected || Index < 0 || Index >= static_cast<int>(PickerVnums.size())) return;
            const int Other = PickerVnums[Index];
            if (Other == Selected) {
                SetStatus(L"An item can't link to itself.", BadColor);
                return;
            }
            const auto Count = ParseNumber(Section->Count.Edit ? Section->Count.Edit->GetText() : L"");
            const int Amount = Count && *Count > 0 ? static_cast<int>(std::min<int64_t>(*Count, 1'000'000)) : 1;
            if (Section->Kind == LinkKind::ComesFrom) {
                ValueRules::AddSource(Selected, Other, Amount);
                SetStatus(L"Comes from " + (Amount > 1 ? std::to_wstring(Amount) + L" x " : L"") + NameOf(Other) + L".", GoodColor);
            } else {
                ValueRules::AddConversion(Selected, Other, Amount);
                SetStatus((Amount > 1 ? std::to_wstring(Amount) + L" of these turn into " : L"Turns into ") + NameOf(Other) + L".", GoodColor);
            }
            RefreshAll();
        }

        void __cdecl OnRemoveLink(void* Argument) {
            const auto* Row = static_cast<LinkRow*>(Argument);
            if (!Selected || !Row->Owner) return;
            const auto Index = static_cast<size_t>(Row->Owner->Scroll + Row->Slot);
            if (Row->Owner->Kind == LinkKind::ComesFrom) ValueRules::RemoveSource(Selected, Index);
            else ValueRules::RemoveConversion(Selected, Index);
            RefreshAll();
        }

        void __cdecl OnTab(void* Argument) {
            ShowTab(static_cast<int>(reinterpret_cast<intptr_t>(Argument)));
        }

        void __cdecl OnPrevPage(void* Argument) {
            auto* Section = static_cast<LinkSection*>(Argument);
            Section->Scroll -= VisibleRows;
            RefreshSection(*Section);
        }

        void __cdecl OnNextPage(void* Argument) {
            auto* Section = static_cast<LinkSection*>(Argument);
            Section->Scroll += VisibleRows;
            RefreshSection(*Section);
        }

        void __cdecl OnValuedChanged(void*) {
            if (!Selected) return;
            ValueRules::SetValued(Selected, ValuedValue);
            RefreshValue();
        }

        void __cdecl OnRandomChanged(void*) {
            if (!Selected) return;
            ValueRules::SetRandomConversion(Selected, RandomValue);
            RefreshValue();
        }

        void FillPickers() {
            CatalogReady = ItemCatalog::Load();
            std::vector<std::wstring> Names;
            std::vector<int16_t> Icons;
            PickerVnums.clear();
            for (const auto& Item : ItemCatalog::All()) {
                Names.push_back(Item.Name);
                Icons.push_back(Item.Icon);
                PickerVnums.push_back(Item.Vnum);
            }
            if (ItemPicker) ItemPicker->SetItems(Names, Icons);
            for (LinkSection& Section : Sections) {
                if (Section.Picker) Section.Picker->SetItems(Names, Icons);
            }
        }

        void PollFields() {
            if (!Selected) return;
            if (AmountBox.Edit) {
                const std::wstring Text = AmountBox.Edit->GetText();
                if (Text != AppliedAmount) {
                    AppliedAmount = Text;
                    if (const auto Amount = ParseNumber(Text)) {
                        ValueRules::SetBuyAmount(Selected, static_cast<int>(std::min<int64_t>(*Amount, 100'000'000)));
                        RefreshValue();
                    } else SetStatus(L"Buy amount: numbers only.", BadColor);
                }
            }
            if (FixedBox.Edit) {
                const std::wstring Text = FixedBox.Edit->GetText();
                if (Text != AppliedFixed) {
                    AppliedFixed = Text;
                    if (const auto Price = ParseNumber(Text)) {
                        ValueRules::SetFixedPrice(Selected, *Price);
                        RefreshValue();
                    } else SetStatus(L"Fixed price: numbers only.", BadColor);
                }
            }
        }

        void BuildSection(LinkSection& Section) {
            Section.Group = Widget::Create<TLBSWidget>(Host);
            if (!Section.Group) return;
            Place(Section.Group, 0, static_cast<int16_t>(Section.Top - HeaderTop), WindowWidth, static_cast<int16_t>(StatusTop - Section.Top - 2));
            Attach(Editors, Section.Group);
            TLBSWidget* Parent = Section.Group;
            auto Y = [&](const int Top) { return static_cast<int16_t>(Top - Section.Top); };
            if (TEWLabel* Header = AddLabel(Parent, Pad, Y(Section.Top), ContentWidth, 1, Section.Title)) Header->textColor = HintColor;
            AddLabel(Parent, Pad, Y(Section.Top + SectionAddOffset + 4), 20, 1, L"x");
            Section.Count = WidgetKit::CreateEditBox(Host, Pad + 12, Y(Section.Top + SectionAddOffset), CountWidth, 22);
            Attach(Parent, Section.Count.Frame);
            SetBoxText(Section.Count, L"1", nullptr);
            for (int i = 0; i < VisibleRows; i++) {
                LinkRow& Row = Section.Rows[i];
                Row.Owner = &Section;
                Row.Slot = i;
                const auto RowY = Y(Section.Top + SectionRowsOffset + i * RowHeight);
                Row.Icon = WidgetKit::CreateItemIcon(Host, Pad, static_cast<int16_t>(RowY + 1), IconSize, -1);
                Attach(Parent, Row.Icon);
                Row.Name = AddLabel(Parent, NameX, static_cast<int16_t>(RowY + 4), NameWidth, 1, L"");
                Row.Remove = AddSpriteButton(Parent, {{244, 2, 20, 20}, {264, 2, 20, 20}, {244, 22, 20, 20}}, &OnRemoveLink, &Row);
                Place(Row.Remove, RemoveX, RowY, RemoveSize, RemoveSize);
            }
            Section.Empty = AddLabel(Parent, Pad, Y(Section.Top + SectionRowsOffset + 4), ContentWidth, 1, Section.EmptyText);
            if (Section.Empty) Section.Empty->textColor = HintColor;

            Section.PrevPage = AddSpriteButton(Parent, {{476, 71, 12, 19}, {476, 49, 12, 19}, {476, 71, 12, 19}}, &OnPrevPage, &Section);
            Section.NextPage = AddSpriteButton(Parent, {{495, 71, 12, 19}, {495, 49, 12, 19}, {495, 71, 12, 19}}, &OnNextPage, &Section);
            Place(Section.PrevPage, Pad, Y(PagerTop), 12, 19);
            Place(Section.NextPage, static_cast<int16_t>(WindowWidth - Pad - 12), Y(PagerTop), 12, 19);
            Section.PageLabel = AddLabel(Parent, Pad + 12, Y(PagerTop + 3), ContentWidth - 24, 3, L"");
            Show(Section.PrevPage, false);
            Show(Section.NextPage, false);
            Show(Section.PageLabel, false);
        }

        void BuildSectionPicker(LinkSection& Section) {
            WidgetKit::Dropdown::Desc Desc;
            Desc.X = Pad + 12 + CountWidth + 6;
            Desc.Y = static_cast<int16_t>(Section.Top + SectionAddOffset);
            Desc.Width = static_cast<int16_t>(ContentWidth - 12 - CountWidth - 6 - DropdownArrow);
            Desc.VisibleRows = 8;
            Desc.Placeholder = Section.Placeholder;
            Desc.ShowIcons = true;
            Desc.OnPick = &OnLinkPicked;
            Desc.Argument = &Section;
            Section.Picker = WidgetKit::Dropdown::Create(Host, Desc);
            if (Section.Picker) {
                Attach(Pages[1], Section.Picker->Widget());
                Show(Section.Picker->Widget(), false);
            }
        }

        void ShowPage(const int Index) {
            ActivePage = Index;
            for (int i = 0; i < 2; i++) {
                Show(Pages[i], i == Index);
                WidgetKit::SetTabActive(PageTabs[i], i == Index);
            }
            if (Index != 1) CloseDropdowns();
        }

        void __cdecl OnPageTab(void* Argument) {
            ShowPage(static_cast<int>(reinterpret_cast<intptr_t>(Argument)));
        }

        void __cdecl OnFilterChanged(void*) {
            if (bool* Enabled = BazaarFilter::EnabledFlag()) *Enabled = FilterValue;
        }

        void BuildBazaarFilterPage(TLBSWidget* Page) {
            FilterBox = WidgetKit::CreateCheckbox(Host, Pad, PickerTop, &FilterValue, &OnFilterChanged);
            if (FilterBox) Attach(Page, FilterBox);
            AddLabel(Page, Pad + 19, PickerTop, ContentWidth - 19, 1, L"Bazaar Filter on");
            const wchar_t* Lines[] = {
                L"Open the NosBazaar on the Buy tab: the filter panel",
                L"sits next to it. Pick the stats you want there and",
                L"the matching listings get marked.",
                L"",
                L"The Filter button under \"Search criteria\" turns it",
                L"on and off too.",
            };
            int16_t Y = PickerTop + 34;
            for (const wchar_t* Line : Lines) {
                if (TEWLabel* Label = AddLabel(Page, Pad, Y, ContentWidth, 1, Line)) Label->textColor = HintColor;
                Y += 16;
            }
        }

        void BuildWindow(TLBSWidget* Root) {
            const WidgetKit::WindowDesc Desc{WidgetKit::WindowStyle::Plain, 240, 100, WindowWidth,
                                             static_cast<uint16_t>(WindowHeight + PageShift), L"Market", true};
            Window = WidgetKit::CreateGameWindow(Host, Desc);
            WidgetKit::CloseOnEscape(Host, Window);
            if (!Window) return;
            Window->isVisible = false;
            Window->isRestrictedToScreen = true;

            for (int i = 0; i < 2; i++) {
                Pages[i] = Widget::Create<TLBSWidget>(Host);
                if (!Pages[i]) continue;
                Place(Pages[i], 0, i == 1 ? PageShift : 0, WindowWidth, static_cast<int16_t>(WindowHeight + (i == 1 ? 0 : PageShift)));
                Attach(Window, Pages[i]);
            }
            constexpr int16_t PageTabWidth = (ContentWidth - 6) / 2;
            const wchar_t* PageNames[] = {L"Bazaar Filter", L"Character Value"};
            for (int i = 0; i < 2; i++) {
                PageTabs[i] = WidgetKit::CreateTab(Host, static_cast<int16_t>(Pad + i * (PageTabWidth + 6)), PageTabsTop, PageTabWidth,
                                                   PageNames[i], &OnPageTab, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
                Attach(Window, PageTabs[i].Button);
            }
            if (Pages[0]) BuildBazaarFilterPage(Pages[0]);

            Editors = Widget::Create<TLBSWidget>(Host);
            if (Editors) {
                Place(Editors, 0, HeaderTop, WindowWidth, static_cast<int16_t>(StatusTop - HeaderTop - 2));
                Editors->isVisible = false;
                Attach(Pages[1], Editors);
            }
            auto Y = [](const int Top) { return static_cast<int16_t>(Top - HeaderTop); };

            HeaderIconWidget = WidgetKit::CreateItemIcon(Host, Pad, Y(HeaderTop), HeaderIcon, -1);
            Attach(Editors, HeaderIconWidget);
            HeaderName = AddLabel(Editors, Pad + HeaderIcon + 8, Y(HeaderTop + 2), ContentWidth - HeaderIcon - 8, 1, L"");
            HeaderVnum = AddLabel(Editors, Pad + HeaderIcon + 8, Y(HeaderTop + 18), ContentWidth - HeaderIcon - 8, 1, L"");
            ValueLabel = AddLabel(Editors, Pad, Y(ValueTop), ContentWidth, 1, L"");

            ValuedBox = WidgetKit::CreateLabeledCheckbox(Host, Pad, Y(ValuedTop), L"Valued (shown in the inventory)", 220,
                                                         &ValuedValue, &OnValuedChanged);
            Attach(Editors, ValuedBox);

            AmountLabel = AddLabel(Editors, Pad, Y(AmountTop + 4), FieldLabelWidth, 1, L"Buy amount");
            AmountBox = WidgetKit::CreateEditBox(Host, Pad + FieldLabelWidth, Y(AmountTop), 90, 22);
            Attach(Editors, AmountBox.Frame);
            AddLabel(Editors, Pad, Y(FixedTop + 4), FieldLabelWidth, 1, L"Fixed price");
            FixedBox = WidgetKit::CreateEditBox(Host, Pad + FieldLabelWidth, Y(FixedTop), 120, 22);
            Attach(Editors, FixedBox.Frame);
            if (TEWLabel* Hint = AddLabel(Editors, Pad + FieldLabelWidth + 126, Y(FixedTop + 4), ContentWidth - FieldLabelWidth - 126, 1, L"e.g. NPC shop")) {
                Hint->textColor = HintColor;
            }

            Sections[0] = {};
            Sections[0].Kind = LinkKind::ComesFrom;
            Sections[0].Top = SectionTop;
            Sections[0].Title = L"Cheaper ways to get this item; the cheapest wins.";
            Sections[0].Placeholder = L"+ Comes from (type to search)";
            Sections[0].EmptyText = L"None. Pick what this item can be had from.";
            Sections[1] = {};
            Sections[1].Kind = LinkKind::TurnsInto;
            Sections[1].Top = SectionTop;
            Sections[1].Title = L"What x of this item can be turned into; the best wins.";
            Sections[1].Placeholder = L"+ Turns into (type to search)";
            Sections[1].EmptyText = L"None. Pick what this item can be turned into.";
            for (LinkSection& Section : Sections) BuildSection(Section);

            constexpr int16_t TabWidth = (ContentWidth - 6) / 2;
            Sections[0].Tab = AddBlueButton(Editors, Pad, Y(TabsTop), TabWidth, L"Comes from", &OnTab,
                                            reinterpret_cast<void*>(intptr_t{0}), &Sections[0].TabCaption);
            Sections[1].Tab = AddBlueButton(Editors, static_cast<int16_t>(Pad + TabWidth + 6), Y(TabsTop), TabWidth, L"Turns into", &OnTab,
                                            reinterpret_cast<void*>(intptr_t{1}), &Sections[1].TabCaption);

            RandomBox = WidgetKit::CreateLabeledCheckbox(Host, Pad, static_cast<int16_t>(RandomTop - SectionTop),
                                                         L"Random outcome: use the average, not the best", 260, &RandomValue, &OnRandomChanged);
            Attach(Sections[1].Group, RandomBox);

            Status = AddLabel(Pages[1], Pad, StatusTop, ContentWidth, 1, L"");
            TableBox = WidgetKit::CreateLabeledCheckbox(Host, Pad, TableTop, L"Show the data table", 200, &TableValue);
            Attach(Pages[1], TableBox);

            BuildSectionPicker(Sections[1]);
            BuildSectionPicker(Sections[0]);
            ShowTab(0);

            WidgetKit::Dropdown::Desc PickerDesc;
            PickerDesc.X = Pad;
            PickerDesc.Y = PickerTop;
            PickerDesc.Width = ContentWidth - DropdownArrow;
            PickerDesc.VisibleRows = 12;
            PickerDesc.Placeholder = L"Pick an item (type to search)";
            PickerDesc.ShowIcons = true;
            PickerDesc.OnPick = &OnItemPicked;
            ItemPicker = WidgetKit::Dropdown::Create(Host, PickerDesc);
            if (ItemPicker) Attach(Pages[1], ItemPicker->Widget());

            FillPickers();
            SetStatus(CatalogReady ? L"Pick an item to edit how it is valued." : L"Couldn't read the game's item list.",
                      CatalogReady ? HintColor : BadColor);
            ShowPage(ActivePage);
            Attach(Root, Window);
        }

        void ForgetWindow() {
            Window = nullptr;
            for (int i = 0; i < 2; i++) {
                Pages[i] = nullptr;
                PageTabs[i] = {};
            }
            FilterBox = nullptr;
            ItemPicker = nullptr;
            HeaderIconWidget = nullptr;
            HeaderName = HeaderVnum = ValueLabel = AmountLabel = Status = nullptr;
            ValuedBox = RandomBox = TableBox = Editors = nullptr;
            AmountBox = FixedBox = {};
            for (LinkSection& Section : Sections) Section = {};
            Selected = 0;
        }

        void CloseDropdowns() {
            if (ItemPicker) ItemPicker->SetOpen(false);
            for (LinkSection& Section : Sections) {
                if (Section.Picker) Section.Picker->SetOpen(false);
            }
        }
    }

    void Startup(const ModHost* ModHost) {
        Host = ModHost;
    }

    void Shutdown() {
        Detach(Window);
        ForgetWindow();
        AttachedRoot = nullptr;
        Host = nullptr;
    }

    void Toggle() {
        PendingToggle = true;
    }

    void Select(const int Vnum) {
        PendingSelect = Vnum;
    }

    bool WantsDataTable() {
        return TableValue && Window && Window->isVisible;
    }

    void EarlyTick(TLBSWidget* Root, const TickContext& Context) {
        if (!Host || !Root || !Root->childrenList) return;
        static WidgetKit::WindowRaiser Raiser;
        Raiser.Tick(Window, Context);
        if (!Context.isPlayerLoaded) {
            Show(Window, false);
            return;
        }
        if (Root != AttachedRoot) {
            ForgetWindow();
            AttachedRoot = Root;
            BuildWindow(Root);
        }
        if (!Window) return;

        if (PendingToggle || PendingSelect) {
            const bool Open = PendingSelect ? true : !Window->isVisible;
            PendingToggle = false;
            if (Open && !CatalogReady) FillPickers();
            if (!Open) CloseDropdowns();
            Show(Window, Open);
            if (Open) Window->BubbleUp();
            if (PendingSelect) SelectItem(PendingSelect);
            PendingSelect = 0;
        }
        if (!Window->isVisible) return;

        if (const bool* Enabled = BazaarFilter::EnabledFlag(); Enabled && *Enabled != FilterValue) {
            FilterValue = *Enabled;
            WidgetKit::SetChecked(FilterBox, FilterValue);
        }
        if (ActivePage != 1) return;

        PollFields();
        if (Selected && ValueRules::Revision() != ShownRevision) {
            ShownRevision = ValueRules::Revision();
            RefreshAll();
        }
        if (ItemPicker) ItemPicker->Tick(Context);
        for (LinkSection& Section : Sections) {
            if (Section.Picker) Section.Picker->Tick(Context);
        }
    }
}
