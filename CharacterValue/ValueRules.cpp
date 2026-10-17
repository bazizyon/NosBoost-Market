#define NOMINMAX
#include <windows.h>

#include "ValueRules.h"
#include "ItemCatalog.h"
#include "BazaarLog.h"
#include "Paths.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ValueRules {
    namespace {
        constexpr char DefaultRules[] =
            "# How Character Value values items. Saved changes are picked up in game automatically.\n"
            "#\n"
            "#   default amount 1          buy amount for items not in amounts.txt\n"
            "#   vnum 1012 manual 500      fixed value per unit\n"
            "#   vnum 1013 none            never valued\n"
            "#   packages on               also count \"whole package only\" listings\n"
            "#\n"
            "# amounts.txt holds the buy amount per item, unvalued.txt the items switched off (both set in F9).\n"
            "# sources.txt lists cheaper ways to get an item (a skin from its chest),\n"
            "# conversions.txt what an item can be turned into (raid tokens at a shop).\n"
            "# values.json from the scraper overrides everything.\n"
            "\n"
            "default amount 1\n";

        int DefaultAmount = 1;
        std::unordered_map<int, int64_t> Manual;
        std::unordered_map<int, bool> Never;
        std::map<int, int> Amounts;
        std::set<int> Unvalued;
        std::map<int, int64_t> Fixed;

        std::map<int, std::vector<SourceLink>> Sources;
        std::map<int, std::vector<SourceLink>> Conversions;
        std::set<int> RandomConversions;
        constexpr int MaxSourceDepth = 8;
        std::unordered_map<int, int64_t> External;
        bool CountPackages = false;

        std::filesystem::file_time_type RulesTime{}, AmountsTime{}, ValuesTime{}, UnvaluedTime{}, SourcesTime{}, FixedTime{}, ConversionsTime{};
        bool SourcesLoaded = false;
        bool ConversionsLoaded = false;
        bool RulesLoaded = false;
        uint32_t SeenMarket = UINT32_MAX;
        uint32_t CurrentRevision = 0;

        std::filesystem::path RulesPath() { return Paths::DataDir() / L"rules.txt"; }
        std::filesystem::path AmountsPath() { return Paths::DataDir() / L"amounts.txt"; }
        std::filesystem::path ValuesPath() { return Paths::DataDir() / L"values.json"; }
        std::filesystem::path UnvaluedPath() { return Paths::DataDir() / L"unvalued.txt"; }
        std::filesystem::path SourcesPath() { return Paths::DataDir() / L"sources.txt"; }
        std::filesystem::path FixedPath() { return Paths::DataDir() / L"fixed.txt"; }
        std::filesystem::path ConversionsPath() { return Paths::DataDir() / L"conversions.txt"; }

        std::filesystem::file_time_type TimeOf(const std::filesystem::path& Path) {
            std::error_code Error;
            const auto Time = std::filesystem::last_write_time(Path, Error);
            return Error ? std::filesystem::file_time_type{} : Time;
        }

        void LoadRules() {
            if (!std::filesystem::exists(RulesPath())) {
                std::ofstream(RulesPath(), std::ios::binary) << DefaultRules;
            }
            DefaultAmount = 1;
            Manual.clear();
            Never.clear();
            CountPackages = false;
            std::ifstream In(RulesPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                std::string Key, Kind;
                if (!(Words >> Key) || Key[0] == '#') continue;
                if (Key == "default") {
                    Words >> Kind;
                    int Amount = 1;
                    if (Kind == "min") DefaultAmount = 1;
                    else if ((Kind == "amount" || Kind == "fill") && (Words >> Amount) && Amount > 0) DefaultAmount = Amount;
                } else if (Key == "vnum") {
                    int Vnum = 0;
                    if (!(Words >> Vnum >> Kind)) continue;
                    int64_t Value = 0;
                    if (Kind == "none") Never[Vnum] = true;
                    else if (Kind == "manual" && (Words >> Value) && Value >= 0) Manual[Vnum] = Value;
                } else if (Key == "packages") {
                    Words >> Kind;
                    CountPackages = Kind == "on";
                }
            }
        }

        void LoadAmounts() {
            Amounts.clear();
            std::ifstream In(AmountsPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                int Vnum = 0, Amount = 0;
                if (Line.empty() || Line[0] == '#' || !(Words >> Vnum >> Amount) || Amount <= 0) continue;
                Amounts[Vnum] = Amount;
            }
        }

        void SaveAmounts() {
            std::ofstream Out(AmountsPath(), std::ios::binary | std::ios::trunc);
            Out << "# <vnum> <buy amount>, set from the F9 window\n";
            for (const auto& [Vnum, Amount] : Amounts) Out << Vnum << ' ' << Amount << '\n';
            Out.close();
            AmountsTime = TimeOf(AmountsPath());
        }

        void LoadUnvalued() {
            Unvalued.clear();
            std::ifstream In(UnvaluedPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                int Vnum = 0;
                if (!Line.empty() && Line[0] != '#' && (Words >> Vnum)) Unvalued.insert(Vnum);
            }
        }

        void SaveUnvalued() {
            std::ofstream Out(UnvaluedPath(), std::ios::binary | std::ios::trunc);
            Out << "# items that are never valued (one vnum per line), set from the F9 window\n";
            for (const int Vnum : Unvalued) Out << Vnum << '\n';
            Out.close();
            UnvaluedTime = TimeOf(UnvaluedPath());
        }

        void LoadFixed() {
            Fixed.clear();
            std::ifstream In(FixedPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                int Vnum = 0;
                int64_t Price = 0;
                if (Line.empty() || Line[0] == '#' || !(Words >> Vnum >> Price) || Price <= 0) continue;
                Fixed[Vnum] = Price;
            }
        }

        void SaveFixed() {
            std::ofstream Out(FixedPath(), std::ios::binary | std::ios::trunc);
            Out << "# <vnum> <price>: fixed price (e.g. an NPC shop), used as the value as is; set from the F9 window\n";
            for (const auto& [Vnum, Price] : Fixed) Out << Vnum << ' ' << Price << '\n';
            Out.close();
            FixedTime = TimeOf(FixedPath());
        }

        constexpr char DefaultSources[] =
            "# <item vnum> <source vnum> [count]\n"
            "# The item can also be had as count x source, e.g. a weapon skin from its skin chest.\n"
            "# It is worth whichever is cheaper: its own bazaar price or a source's value.\n"
            "# An item can have several sources (one line each); sources can have sources too.\n"
            "\n";

        void LoadSources() {
            if (!std::filesystem::exists(SourcesPath())) {
                std::ofstream(SourcesPath(), std::ios::binary) << DefaultSources;
            }
            Sources.clear();
            std::ifstream In(SourcesPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                int Item = 0, From = 0, Count = 1;
                if (Line.empty() || Line[0] == '#' || !(Words >> Item >> From)) continue;
                if (!(Words >> Count) || Count < 1) Count = 1;
                if (Item != From) Sources[Item].push_back({From, Count});
            }
        }

        std::string NameForComment(const int Vnum) {
            const ItemCatalog::Item* Item = ItemCatalog::Find(Vnum);
            if (!Item || Item->Name.empty()) return std::to_string(Vnum);
            const int Size = WideCharToMultiByte(CP_UTF8, 0, Item->Name.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string Name(Size > 0 ? Size - 1 : 0, '\0');
            if (Size > 1) WideCharToMultiByte(CP_UTF8, 0, Item->Name.c_str(), -1, Name.data(), Size, nullptr, nullptr);
            return Name;
        }

        void SaveSources() {
            std::ofstream Out(SourcesPath(), std::ios::binary | std::ios::trunc);
            Out << DefaultSources;
            for (const auto& [Item, Links] : Sources) {
                for (const SourceLink& Link : Links) {
                    Out << Item << ' ' << Link.Vnum << ' ' << Link.Count << "    # " << NameForComment(Item) << " <- ";
                    if (Link.Count > 1) Out << Link.Count << " x ";
                    Out << NameForComment(Link.Vnum) << '\n';
                }
            }
            Out.close();
            SourcesTime = TimeOf(SourcesPath());
        }

        constexpr char DefaultConversions[] =
            "# <item vnum> <output vnum> [count]: count of the item can be turned into one output,\n"
            "# e.g. raid tokens at a shop. The item is worth at least its best output / count.\n"
            "# random <item vnum>: the item gives one of its outputs at random, so use the average instead.\n"
            "\n";

        void LoadConversions() {
            if (!std::filesystem::exists(ConversionsPath())) {
                std::ofstream(ConversionsPath(), std::ios::binary) << DefaultConversions;
            }
            Conversions.clear();
            RandomConversions.clear();
            std::ifstream In(ConversionsPath());
            std::string Line;
            while (std::getline(In, Line)) {
                std::istringstream Words(Line);
                std::string First;
                if (Line.empty() || Line[0] == '#' || !(Words >> First)) continue;
                if (First == "random") {
                    int Item = 0;
                    if (Words >> Item) RandomConversions.insert(Item);
                    continue;
                }
                int Output = 0, Count = 1;
                const int Item = std::atoi(First.c_str());
                if (!Item || !(Words >> Output)) continue;
                if (!(Words >> Count) || Count < 1) Count = 1;
                if (Item != Output) Conversions[Item].push_back({Output, Count});
            }
        }

        void SaveConversions() {
            std::ofstream Out(ConversionsPath(), std::ios::binary | std::ios::trunc);
            Out << DefaultConversions;
            for (const int Item : RandomConversions) Out << "random " << Item << "    # " << NameForComment(Item) << '\n';
            for (const auto& [Item, Links] : Conversions) {
                for (const SourceLink& Link : Links) {
                    Out << Item << ' ' << Link.Vnum << ' ' << Link.Count << "    # ";
                    if (Link.Count > 1) Out << Link.Count << " x ";
                    Out << NameForComment(Item) << " -> " << NameForComment(Link.Vnum) << '\n';
                }
            }
            Out.close();
            ConversionsTime = TimeOf(ConversionsPath());
        }

        void LoadValues() {
            External.clear();
            std::ifstream In(ValuesPath(), std::ios::binary);
            const std::string Text((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
            size_t At = 0;
            while ((At = Text.find('"', At)) != std::string::npos) {
                const size_t KeyEnd = Text.find('"', At + 1);
                if (KeyEnd == std::string::npos) break;
                const std::string Key = Text.substr(At + 1, KeyEnd - At - 1);
                At = KeyEnd + 1;
                const size_t Colon = Text.find_first_not_of(" \t\r\n", At);
                if (Colon == std::string::npos || Text[Colon] != ':') continue;
                const size_t Number = Text.find_first_not_of(" \t\r\n", Colon + 1);
                if (Number == std::string::npos || !(std::isdigit(static_cast<unsigned char>(Text[Number])))) continue;
                if (Key.empty() || !std::all_of(Key.begin(), Key.end(), [](const char C) { return C >= '0' && C <= '9'; })) continue;
                External[std::stoi(Key)] = std::stoll(Text.substr(Number, 19));
                At = Number;
            }
        }

        std::optional<int64_t> FillPrice(const std::vector<BazaarLog::Listing>& Listings, const int64_t Wanted) {
            std::vector<const BazaarLog::Listing*> Usable;
            for (const auto& Entry : Listings) {
                if (Entry.Amount <= 0) continue;
                if (Entry.IsPackage && !CountPackages) continue;
                Usable.push_back(&Entry);
            }
            std::sort(Usable.begin(), Usable.end(), [](const auto* A, const auto* B) { return A->Price < B->Price; });

            int64_t Left = Wanted, Bought = 0, Cost = 0;
            for (const auto* Entry : Usable) {
                if (Left <= 0) break;
                if (Entry->IsPackage && Entry->Amount > Left) continue;
                const int64_t Take = Entry->IsPackage ? Entry->Amount : std::min<int64_t>(Entry->Amount, Left);
                Cost += Take * Entry->Price;
                Bought += Take;
                Left -= Take;
            }
            if (Bought == 0) return std::nullopt;
            return (Cost + Bought / 2) / Bought;
        }
    }

    void Refresh() {
        bool Changed = false;
        if (const auto Time = TimeOf(RulesPath()); !RulesLoaded || Time != RulesTime) {
            LoadRules();
            RulesTime = TimeOf(RulesPath());
            RulesLoaded = true;
            Changed = true;
        }
        if (const auto Time = TimeOf(AmountsPath()); Time != AmountsTime) {
            LoadAmounts();
            AmountsTime = Time;
            Changed = true;
        }
        if (const auto Time = TimeOf(SourcesPath()); !SourcesLoaded || Time != SourcesTime) {
            LoadSources();
            SourcesTime = TimeOf(SourcesPath());
            SourcesLoaded = true;
            Changed = true;
        }
        if (const auto Time = TimeOf(ConversionsPath()); !ConversionsLoaded || Time != ConversionsTime) {
            LoadConversions();
            ConversionsTime = TimeOf(ConversionsPath());
            ConversionsLoaded = true;
            Changed = true;
        }
        if (const auto Time = TimeOf(FixedPath()); Time != FixedTime) {
            LoadFixed();
            FixedTime = Time;
            Changed = true;
        }
        if (const auto Time = TimeOf(UnvaluedPath()); Time != UnvaluedTime) {
            LoadUnvalued();
            UnvaluedTime = Time;
            Changed = true;
        }
        if (const auto Time = TimeOf(ValuesPath()); Time != ValuesTime) {
            LoadValues();
            ValuesTime = Time;
            Changed = true;
        }
        if (BazaarLog::Revision() != SeenMarket) {
            SeenMarket = BazaarLog::Revision();
            Changed = true;
        }
        if (Changed) CurrentRevision++;
    }

    namespace {
        std::optional<Value> ValueOf(const int Vnum, std::vector<int>& Path) {
            if (const auto Found = External.find(Vnum); Found != External.end()) return Value{Found->second, Source::External};
            if (Never.count(Vnum)) return std::nullopt;
            if (const auto Found = Manual.find(Vnum); Found != Manual.end()) return Value{Found->second, Source::Manual};
            if (const auto Npc = Fixed.find(Vnum); Npc != Fixed.end()) return Value{Npc->second, Source::Fixed};
            if (std::find(Path.begin(), Path.end(), Vnum) != Path.end() || Path.size() >= MaxSourceDepth) return std::nullopt;
            Path.push_back(Vnum);

            std::optional<Value> Best;
            if (const BazaarLog::Snapshot* Shot = BazaarLog::Find(Vnum)) {
                if (const auto Price = FillPrice(Shot->Listings, BuyAmount(Vnum))) Best = Value{*Price, Source::Bazaar};
            }
            if (const auto Links = Sources.find(Vnum); Links != Sources.end()) {
                for (const SourceLink& Link : Links->second) {
                    const auto FromSource = ValueOf(Link.Vnum, Path);
                    if (!FromSource) continue;
                    const int64_t Cost = FromSource->UnitValue * Link.Count;
                    if (!Best || Cost < Best->UnitValue) Best = Value{Cost, Source::Via, Link.Vnum};
                }
            }

            if (const auto Outputs = Conversions.find(Vnum); Outputs != Conversions.end()) {
                const bool Random = RandomConversions.count(Vnum) != 0;
                std::optional<Value> Converted;
                int64_t Sum = 0;
                int Known = 0;
                for (const SourceLink& Link : Outputs->second) {
                    const auto Output = ValueOf(Link.Vnum, Path);
                    if (!Output) continue;
                    const int64_t PerUnit = Output->UnitValue / Link.Count;
                    Sum += PerUnit;
                    Known++;
                    if (!Converted || PerUnit > Converted->UnitValue) Converted = Value{PerUnit, Source::Converts, Link.Vnum};
                }
                if (Random && Known) Converted = Value{Sum / Known, Source::Converts, 0, Known};
                if (Converted && (!Best || Converted->UnitValue > Best->UnitValue)) Best = Converted;
            }

            Path.pop_back();
            return Best;
        }
    }

    std::optional<Value> Find(const int Vnum) {
        static std::unordered_map<int, std::optional<Value>> Cache;
        static uint32_t CacheRevision = UINT32_MAX;
        if (CacheRevision != CurrentRevision) {
            Cache.clear();
            CacheRevision = CurrentRevision;
        }
        if (const auto Hit = Cache.find(Vnum); Hit != Cache.end()) return Hit->second;
        std::vector<int> Path;
        const auto Result = Unvalued.count(Vnum) ? std::nullopt : ValueOf(Vnum, Path);
        Cache.emplace(Vnum, Result);
        return Result;
    }

    std::optional<int64_t> UnitValue(const int Vnum) {
        const auto Found = Find(Vnum);
        return Found ? std::optional<int64_t>(Found->UnitValue) : std::nullopt;
    }

    int BuyAmount(const int Vnum) {
        const auto Found = Amounts.find(Vnum);
        return Found == Amounts.end() ? DefaultAmount : Found->second;
    }

    bool HasOwnBuyAmount(const int Vnum) {
        return Amounts.count(Vnum) != 0;
    }

    void SetBuyAmount(const int Vnum, const int Amount) {
        if (Amount > 0) Amounts[Vnum] = Amount;
        else Amounts.erase(Vnum);
        SaveAmounts();
        CurrentRevision++;
    }

    int64_t FixedPrice(const int Vnum) {
        const auto Found = Fixed.find(Vnum);
        return Found == Fixed.end() ? 0 : Found->second;
    }

    void SetFixedPrice(const int Vnum, const int64_t Price) {
        if (Price > 0) Fixed[Vnum] = Price;
        else Fixed.erase(Vnum);
        SaveFixed();
        CurrentRevision++;
    }

    std::vector<SourceLink> SourcesOf(const int Vnum) {
        const auto Found = Sources.find(Vnum);
        return Found == Sources.end() ? std::vector<SourceLink>{} : Found->second;
    }

    void AddSource(const int Vnum, const int Source, const int Count) {
        if (Vnum == Source || Count < 1) return;
        auto& Links = Sources[Vnum];
        const auto Existing = std::find_if(Links.begin(), Links.end(), [&](const SourceLink& Link) { return Link.Vnum == Source; });
        if (Existing != Links.end()) Existing->Count = Count;
        else Links.push_back({Source, Count});
        SaveSources();
        CurrentRevision++;
    }

    void RemoveSource(const int Vnum, const size_t Index) {
        const auto Found = Sources.find(Vnum);
        if (Found == Sources.end() || Index >= Found->second.size()) return;
        Found->second.erase(Found->second.begin() + static_cast<ptrdiff_t>(Index));
        if (Found->second.empty()) Sources.erase(Found);
        SaveSources();
        CurrentRevision++;
    }

    std::vector<SourceLink> ConversionsOf(const int Vnum) {
        const auto Found = Conversions.find(Vnum);
        return Found == Conversions.end() ? std::vector<SourceLink>{} : Found->second;
    }

    void AddConversion(const int Vnum, const int Output, const int Count) {
        if (Vnum == Output || Count < 1) return;
        auto& Links = Conversions[Vnum];
        const auto Existing = std::find_if(Links.begin(), Links.end(), [&](const SourceLink& Link) { return Link.Vnum == Output; });
        if (Existing != Links.end()) Existing->Count = Count;
        else Links.push_back({Output, Count});
        SaveConversions();
        CurrentRevision++;
    }

    void RemoveConversion(const int Vnum, const size_t Index) {
        const auto Found = Conversions.find(Vnum);
        if (Found == Conversions.end() || Index >= Found->second.size()) return;
        Found->second.erase(Found->second.begin() + static_cast<ptrdiff_t>(Index));
        if (Found->second.empty()) Conversions.erase(Found);
        SaveConversions();
        CurrentRevision++;
    }

    bool IsRandomConversion(const int Vnum) {
        return RandomConversions.count(Vnum) != 0;
    }

    void SetRandomConversion(const int Vnum, const bool Random) {
        if (Random) RandomConversions.insert(Vnum);
        else RandomConversions.erase(Vnum);
        SaveConversions();
        CurrentRevision++;
    }

    bool IsValued(const int Vnum) {
        return Unvalued.count(Vnum) == 0;
    }

    void SetValued(const int Vnum, const bool Valued) {
        if (Valued) Unvalued.erase(Vnum);
        else Unvalued.insert(Vnum);
        SaveUnvalued();
        CurrentRevision++;
    }

    uint32_t Revision() {
        return CurrentRevision;
    }
}
