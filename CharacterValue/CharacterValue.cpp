#define NOMINMAX
#include <windows.h>

#include "ModContract.h"
#include "Features.h"
#include "TLBSWidget.h"
#include "TEWLabel.h"
#include "TEWEditWidget.h"
#include "TEWButtonWidget.h"
#include "TEWCustomPanelWidget.h"
#include "TEWGraphicButtonWidget.h"
#include "TNTIconWidget.h"
#include "HoverHint.h"
#include "InventoryOverlay.h"
#include "ItemEditor.h"
#include "ItemCatalog.h"
#include "BazaarLog.h"
#include "SearchHotkey.h"
#include "ValueRules.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    const ModHost* Host = nullptr;
    bool CatalogLoaded = false;
    std::unordered_map<int, int> PendingAmounts;
    std::unordered_map<int, int> PendingFixed;

    constexpr ModClassRequirement Requirements[] = {
        {TLBSWidget::ClassName, TLBSWidget::Version, TLBSWidget::ExpectedSize},
        {TEWCustomPanelWidget::ClassName, TEWCustomPanelWidget::Version, TEWCustomPanelWidget::ExpectedSize},
        {TEWGraphicButtonWidget::ClassName, TEWGraphicButtonWidget::Version, TEWGraphicButtonWidget::ExpectedSize},
        {TEWButtonWidget::ClassName, TEWButtonWidget::Version, TEWButtonWidget::ExpectedSize},
        {TEWEditWidget::ClassName, TEWEditWidget::Version, TEWEditWidget::ExpectedSize},
        {TEWLabel::ClassName, TEWLabel::Version, TEWLabel::ExpectedSize},
        {TNTIconWidget::ClassName, TNTIconWidget::Version, TNTIconWidget::ExpectedSize},
    };

    std::string Utf8(const std::wstring& Text) {
        if (Text.empty()) return {};
        const int Size = WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), nullptr, 0, nullptr, nullptr);
        std::string Result(Size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, Text.c_str(), static_cast<int>(Text.size()), Result.data(), Size, nullptr, nullptr);
        return Result;
    }

    std::string NameOf(const int Vnum) {
        const ItemCatalog::Item* Item = CatalogLoaded ? ItemCatalog::Find(Vnum) : nullptr;
        return Item ? Utf8(Item->Name) : "#" + std::to_string(Vnum);
    }

    void DrawDataWindow() {
        ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Character Value data")) {
            ImGui::End();
            return;
        }

        struct Row {
            int Vnum;
            const BazaarLog::Snapshot* Shot;
        };
        std::vector<Row> Rows;
        for (const auto& [Vnum, Shot] : BazaarLog::Snapshots()) Rows.push_back({Vnum, &Shot});
        std::sort(Rows.begin(), Rows.end(), [](const Row& A, const Row& B) { return A.Shot->Time > B.Shot->Time; });

        using namespace std::chrono;
        const int64_t Now = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        ImGui::Text("%d items searched", static_cast<int>(Rows.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("(buy amount 1 = cheapest unit, 1000 = average of the cheapest 1000; fixed price 0 = none)");
        constexpr ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("snapshots", 9, Flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Valued", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Item");
            ImGui::TableSetupColumn("Vnum");
            ImGui::TableSetupColumn("Listings");
            ImGui::TableSetupColumn("Cheapest");
            ImGui::TableSetupColumn("Buy amount");
            ImGui::TableSetupColumn("Fixed price");
            ImGui::TableSetupColumn("Value");
            ImGui::TableSetupColumn("Searched");
            ImGui::TableHeadersRow();
            for (const Row& Entry : Rows) {
                int64_t Cheapest = -1;
                for (const auto& Listing : Entry.Shot->Listings) {
                    if (Cheapest < 0 || Listing.Price < Cheapest) Cheapest = Listing.Price;
                }
                const auto Value = ValueRules::Find(Entry.Vnum);
                ImGui::PushID(Entry.Vnum);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                bool Valued = ValueRules::IsValued(Entry.Vnum);
                if (ImGui::Checkbox("##valued", &Valued)) ValueRules::SetValued(Entry.Vnum, Valued);
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Edit")) ItemEditor::Select(Entry.Vnum);
                ImGui::SameLine();
                ImGui::TextUnformatted(NameOf(Entry.Vnum).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d", Entry.Vnum);
                ImGui::TableNextColumn();
                if (Entry.Shot->Listings.empty()) ImGui::TextDisabled("none listed");
                else ImGui::Text("%d", static_cast<int>(Entry.Shot->Listings.size()));
                ImGui::TableNextColumn();
                if (Cheapest >= 0) ImGui::Text("%lld", static_cast<long long>(Cheapest)); else ImGui::TextUnformatted("-");

                ImGui::TableNextColumn();
                const auto Editing = PendingAmounts.find(Entry.Vnum);
                int Amount = Editing != PendingAmounts.end() ? Editing->second : ValueRules::BuyAmount(Entry.Vnum);
                ImGui::SetNextItemWidth(90);
                if (ImGui::InputInt("##amount", &Amount, 0, 0)) PendingAmounts[Entry.Vnum] = std::max(1, Amount);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ValueRules::SetBuyAmount(Entry.Vnum, std::max(1, Amount));
                    PendingAmounts.erase(Entry.Vnum);
                }
                if (!ValueRules::HasOwnBuyAmount(Entry.Vnum)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("default");
                }

                ImGui::TableNextColumn();
                const auto EditingFixed = PendingFixed.find(Entry.Vnum);
                int Fixed = EditingFixed != PendingFixed.end() ? EditingFixed->second
                                                               : static_cast<int>(ValueRules::FixedPrice(Entry.Vnum));
                ImGui::SetNextItemWidth(100);
                if (ImGui::InputInt("##fixed", &Fixed, 0, 0)) PendingFixed[Entry.Vnum] = std::max(0, Fixed);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ValueRules::SetFixedPrice(Entry.Vnum, std::max(0, Fixed));
                    PendingFixed.erase(Entry.Vnum);
                }

                ImGui::TableNextColumn();
                if (!Value) ImGui::TextUnformatted("-");
                else {
                    ImGui::Text("%lld", static_cast<long long>(Value->UnitValue));
                    if (Value->From == ValueRules::Source::Via) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("via %s", NameOf(Value->Via).c_str());
                    } else if (Value->From == ValueRules::Source::Converts) {
                        ImGui::SameLine();
                        if (Value->Via) ImGui::TextDisabled("as %s", NameOf(Value->Via).c_str());
                        else ImGui::TextDisabled("avg of %d", Value->Options);
                    } else if (Value->From == ValueRules::Source::Fixed) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("fixed");
                    } else if (Value->From != ValueRules::Source::Bazaar) {
                        ImGui::SameLine();
                        ImGui::TextDisabled(Value->From == ValueRules::Source::External ? "scraper" : "manual");
                    }
                }
                ImGui::TableNextColumn(); ImGui::Text("%lld min ago", static_cast<long long>((Now - Entry.Shot->Time) / 60000));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }
}

namespace CharacterValue {
    void Startup(const ModHost* ModHost) {
        Host = ModHost;
        BazaarLog::LoadHistory();
        ValueRules::Refresh();
        Host->SetRawPacketObserver(&BazaarLog::OnRawPacket);
        InventoryOverlay::Startup(Host);
        ItemEditor::Startup(Host);
        HoverHint::Startup(Host);
    }

    void Shutdown() {
        if (Host) Host->SetRawPacketObserver(nullptr);
        SearchHotkey::Reset();
        ItemEditor::Shutdown();
        HoverHint::Shutdown();
        InventoryOverlay::Shutdown();
        BazaarLog::CloseLog();
        Host = nullptr;
    }

    void EarlyTick(TLBSWidget* Root, const TickContext tickContext) {
        static int CatalogRetry = 0;
        if (!CatalogLoaded && tickContext.isPlayerLoaded && CatalogRetry-- <= 0) {
            CatalogLoaded = ItemCatalog::Load();
            CatalogRetry = 120;
        }
        static int Frame = 0;
        if (++Frame % 30 == 0) ValueRules::Refresh();
        if (!CatalogLoaded) return;
        InventoryOverlay::EarlyTick(Root, tickContext);
        HoverHint::EarlyTick(Root, InventoryOverlay::Window());
        SearchHotkey::EarlyTick(Root, HoverHint::HoveredItem());
        ItemEditor::EarlyTick(Root, tickContext);
    }

    void Tick() {
        if (ItemEditor::WantsDataTable()) DrawDataWindow();
    }
}
