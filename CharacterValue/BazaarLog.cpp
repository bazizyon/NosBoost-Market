#include "BazaarLog.h"
#include "Paths.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <ctime>
#include <fstream>
#include <unordered_set>

namespace BazaarLog {
    namespace {
        constexpr int64_t SameSearchMs = 10 * 60 * 1000;

        std::unordered_map<int, Snapshot> ByVnum;
        uint32_t CurrentRevision = 0;

        std::vector<int> LastSearchVnums;
        int LastSearchPage = -1;

        FILE* Log = nullptr;
        std::string LogMonth;

        int64_t NowMs() {
            using namespace std::chrono;
            return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }

        std::string MonthOf(const int64_t Ms) {
            const std::time_t Seconds = Ms / 1000;
            std::tm Local{};
            localtime_s(&Local, &Seconds);
            char Text[16];
            std::strftime(Text, sizeof(Text), "%Y-%m", &Local);
            return Text;
        }

        std::filesystem::path LogDir() {
            auto Dir = Paths::DataDir() / L"market";
            std::error_code Error;
            std::filesystem::create_directories(Dir, Error);
            return Dir;
        }

        void WriteLog(const int64_t Ms, const bool IsSend, const char* Packet) {
            const std::string Month = MonthOf(Ms);
            if (!Log || Month != LogMonth) {
                if (Log) std::fclose(Log);
                Log = _wfsopen((LogDir() / (std::wstring(Month.begin(), Month.end()) + L".log")).c_str(), L"ab", _SH_DENYWR);
                LogMonth = Month;
            }
            if (!Log) return;
            std::fprintf(Log, "%lld\t%c\t%s\n", static_cast<long long>(Ms), IsSend ? 'S' : 'R', Packet);
            std::fflush(Log);
        }

        std::vector<std::string_view> Split(const std::string_view Text, const char Separator, const bool SkipEmpty) {
            std::vector<std::string_view> Parts;
            size_t Start = 0;
            while (Start <= Text.size()) {
                const size_t End = std::min(Text.find(Separator, Start), Text.size());
                if (!SkipEmpty || End > Start) Parts.push_back(Text.substr(Start, End - Start));
                Start = End + 1;
            }
            return Parts;
        }

        int64_t ToInt(const std::string_view Text) {
            int64_t Value = 0;
            bool Negative = false;
            size_t i = 0;
            if (i < Text.size() && Text[i] == '-') { Negative = true; i++; }
            for (; i < Text.size() && Text[i] >= '0' && Text[i] <= '9'; i++) Value = Value * 10 + (Text[i] - '0');
            return Negative ? -Value : Value;
        }

        std::vector<std::string_view> Body(const char* Packet, const std::string_view Header) {
            auto Tokens = Split(Packet, ' ', true);
            for (size_t i = 0; i < Tokens.size() && i < 2; i++) {
                if (Tokens[i] == Header) return {Tokens.begin() + static_cast<ptrdiff_t>(i) + 1, Tokens.end()};
            }
            return {};
        }

        void OnSearch(const char* Packet) {
            const auto Fields = Body(Packet, "c_blist");
            if (Fields.size() < 9) return;
            LastSearchPage = static_cast<int>(ToInt(Fields[0]));
            LastSearchVnums.clear();
            const size_t Count = static_cast<size_t>(std::max<int64_t>(0, ToInt(Fields[8])));
            for (size_t i = 0; i < Count && 9 + i < Fields.size(); i++) {
                LastSearchVnums.push_back(static_cast<int>(ToInt(Fields[9 + i])));
            }
        }

        bool ParseListing(const std::string_view Entry, Listing& Out) {
            const auto Fields = Split(Entry, '|', false);
            if (Fields.size() < 8) return false;
            Out.AuctionId = ToInt(Fields[0]);
            Out.SellerId = ToInt(Fields[1]);
            Out.Seller = std::string(Fields[2]);
            Out.Vnum = static_cast<int>(ToInt(Fields[3]));
            Out.Amount = static_cast<int>(ToInt(Fields[4]));
            Out.IsPackage = ToInt(Fields[5]) != 0;
            Out.Price = ToInt(Fields[6]);
            Out.MinutesLeft = static_cast<int>(ToInt(Fields[7]));
            Out.HasStats = Fields.size() > 14 && std::count(Fields[14].begin(), Fields[14].end(), '^') > 1;
            return Out.Vnum > 0;
        }

        void OnResults(const char* Packet, const int64_t Ms) {
            const auto Fields = Body(Packet, "rc_blist");
            if (Fields.empty()) return;
            const int Page = static_cast<int>(ToInt(Fields[0]));

            std::unordered_set<int> Started;
            const auto Start = [&](const int Vnum) {
                if (!Started.insert(Vnum).second) return;
                Snapshot& Shot = ByVnum[Vnum];
                if (Page == 0 || Ms - Shot.Time > SameSearchMs) Shot.Listings.clear();
                Shot.Time = Ms;
            };

            if (Page == 0 && LastSearchPage == 0) {
                for (const int Vnum : LastSearchVnums) Start(Vnum);
            }
            for (size_t i = 1; i < Fields.size(); i++) {
                Listing Entry;
                if (!ParseListing(Fields[i], Entry)) continue;
                Start(Entry.Vnum);
                ByVnum[Entry.Vnum].Listings.push_back(std::move(Entry));
            }
            CurrentRevision++;
        }

        void Apply(const char* Packet, const bool IsSend, const int64_t Ms) {
            if (IsSend) OnSearch(Packet);
            else OnResults(Packet, Ms);
        }
    }

    void OnRawPacket(const char* Packet, const bool IsSend) {
        const char* Header = IsSend ? "c_blist" : "rc_blist";
        if (!Packet || !std::strstr(Packet, Header) || Body(Packet, Header).empty()) return;
        const int64_t Ms = NowMs();
        WriteLog(Ms, IsSend, Packet);
        Apply(Packet, IsSend, Ms);
    }

    void LoadHistory() {
        std::error_code Error;
        std::vector<std::filesystem::path> Files;
        for (const auto& File : std::filesystem::directory_iterator(LogDir(), Error)) {
            if (File.path().extension() == L".log") Files.push_back(File.path());
        }
        std::sort(Files.begin(), Files.end());
        for (const auto& File : Files) {
            std::ifstream In(File, std::ios::binary);
            std::string Line;
            while (std::getline(In, Line)) {
                if (!Line.empty() && Line.back() == '\r') Line.pop_back();
                const size_t Tab1 = Line.find('\t');
                if (Tab1 == std::string::npos || Tab1 + 3 > Line.size() || Line[Tab1 + 2] != '\t') continue;
                const int64_t Ms = ToInt(std::string_view(Line).substr(0, Tab1));
                Apply(Line.c_str() + Tab1 + 3, Line[Tab1 + 1] == 'S', Ms);
            }
        }
        LastSearchVnums.clear();
        LastSearchPage = -1;
        CurrentRevision++;
    }

    void CloseLog() {
        if (Log) std::fclose(Log);
        Log = nullptr;
        LogMonth.clear();
    }

    const std::unordered_map<int, Snapshot>& Snapshots() {
        return ByVnum;
    }

    const Snapshot* Find(const int Vnum) {
        const auto Found = ByVnum.find(Vnum);
        return Found == ByVnum.end() ? nullptr : &Found->second;
    }

    uint32_t Revision() {
        return CurrentRevision;
    }
}
