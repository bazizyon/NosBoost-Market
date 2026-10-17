#pragma once
#include <cstdint>
#include <optional>
#include <vector>

//
namespace ValueRules {
    enum class Source { None, External, Manual, Bazaar, Via, Fixed, Converts };

    struct Value {
        int64_t UnitValue = 0;
        Source From = Source::None;
        int Via = 0;
        int Options = 0;
    };

    void Refresh();
    std::optional<Value> Find(int Vnum);
    std::optional<int64_t> UnitValue(int Vnum);

    int BuyAmount(int Vnum);
    bool HasOwnBuyAmount(int Vnum);
    void SetBuyAmount(int Vnum, int Amount);

    int64_t FixedPrice(int Vnum);
    void SetFixedPrice(int Vnum, int64_t Price);

    struct SourceLink {
        int Vnum = 0;
        int Count = 1;
    };
    std::vector<SourceLink> SourcesOf(int Vnum);
    void AddSource(int Vnum, int Source, int Count);
    void RemoveSource(int Vnum, size_t Index);

    std::vector<SourceLink> ConversionsOf(int Vnum);
    void AddConversion(int Vnum, int Output, int Count);
    void RemoveConversion(int Vnum, size_t Index);
    bool IsRandomConversion(int Vnum);
    void SetRandomConversion(int Vnum, bool Random);

    bool IsValued(int Vnum);
    void SetValued(int Vnum, bool Valued);

    uint32_t Revision();
}
