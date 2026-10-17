#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace BazaarLog {
    struct Listing {
        int64_t AuctionId = 0;
        int64_t SellerId = 0;
        std::string Seller;
        int Vnum = 0;
        int Amount = 0;
        bool IsPackage = false;
        int64_t Price = 0;
        int MinutesLeft = 0;
        bool HasStats = false;
    };

    struct Snapshot {
        int64_t Time = 0;
        std::vector<Listing> Listings;
    };

    void OnRawPacket(const char* Packet, bool IsSend);
    void LoadHistory();
    void CloseLog();

    const std::unordered_map<int, Snapshot>& Snapshots();
    const Snapshot* Find(int Vnum);
    uint32_t Revision();
}
