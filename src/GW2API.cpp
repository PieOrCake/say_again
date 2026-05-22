#include "GW2API.h"
#include "SharedState.h"
#include "nexus/Nexus.h"
#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <string>
#include <cstdarg>

namespace GW2API {

using ChatLinks::LinkType;

struct CachedResult {
    std::string display;   // empty when not yet resolved
    State       state = State::Pending;
};

struct ApiRequest {
    LinkType type;
    uint32_t id;
};

static HINTERNET                   s_Session = nullptr;
static HINTERNET                   s_Connect = nullptr;
static std::thread                 s_Worker;
static std::mutex                  s_Mutex;
static std::condition_variable     s_CV;
static std::queue<ApiRequest>      s_Queue;
static std::atomic<bool>           s_Shutdown{false};
static std::unordered_map<uint64_t, CachedResult> s_Cache;
static std::unordered_set<uint64_t>               s_Pending;

// POI lookup populated lazily by fetching specific floors per continent.
// Different floors are "snapshots" of the world; PoF and newer content live on
// floor 49 of continent 1, while core/HoT/etc. live on floor 1. We merge both.
static std::unordered_map<uint32_t, std::string> s_PoiLookup;
static bool s_FloorLoaded[3] = {false, false, false};  // c1/f1, c1/f49, c2/f1

static uint64_t CacheKey(LinkType t, uint32_t id) {
    return (static_cast<uint64_t>(t) << 32) | id;
}

// --- HTTP ---
static void LogF(const char* fmt, ...) {
    if (!APIDefs) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    APIDefs->Log(LOGL_INFO, "SayAgain", buf);
}

static std::string WPathToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), sz, nullptr, nullptr);
    return s;
}

static std::string HttpGet(const std::wstring& path) {
    if (!s_Connect) { LogF("HttpGet: no connection"); return {}; }
    HINTERNET req = WinHttpOpenRequest(
        s_Connect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req) { LogF("HttpGet: WinHttpOpenRequest failed (err=%lu)", GetLastError()); return {}; }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        LogF("HttpGet: WinHttpSendRequest failed for %s (err=%lu)",
             WPathToUtf8(path).c_str(), GetLastError());
        WinHttpCloseHandle(req);
        return {};
    }
    if (!WinHttpReceiveResponse(req, nullptr)) {
        LogF("HttpGet: WinHttpReceiveResponse failed for %s (err=%lu)",
             WPathToUtf8(path).c_str(), GetLastError());
        WinHttpCloseHandle(req);
        return {};
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(req,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::string body;
    if (statusCode == 200) {
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            WinHttpReadData(req, chunk.data(), avail, &read);
            body.append(chunk.data(), read);
        }
    } else {
        LogF("HttpGet: %s -> status=%lu", WPathToUtf8(path).c_str(), statusCode);
    }

    WinHttpCloseHandle(req);
    return body;
}

// --- POI floor loader ---
static void LoadFloor(int continent, int floor) {
    std::wstring path = L"/v2/continents/" + std::to_wstring(continent)
                      + L"/floors/" + std::to_wstring(floor);
    std::string json_str = HttpGet(path);
    if (json_str.empty()) return;

    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.contains("regions")) return;

        std::unordered_map<uint32_t, std::string> batch;
        for (auto& [rid, region] : j["regions"].items()) {
            if (!region.contains("maps")) continue;
            for (auto& [mid, map] : region["maps"].items()) {
                if (!map.contains("points_of_interest")) continue;
                for (auto& [pid, poi] : map["points_of_interest"].items()) {
                    uint32_t poi_id  = poi.value("id", 0u);
                    std::string name = poi.value("name", "");
                    if (poi_id && !name.empty())
                        batch[poi_id] = std::move(name);
                }
            }
        }

        std::lock_guard<std::mutex> lk(s_Mutex);
        for (auto& [id, name] : batch)
            s_PoiLookup.emplace(id, std::move(name));
    } catch (const std::exception& e) {
        LogF("LoadFloor %d/%d: parse error: %s", continent, floor, e.what());
    } catch (...) {}
}

// Only called on the worker thread.
static CachedResult FetchMapLink(uint32_t poi_id) {
    CachedResult r;
    r.state = State::Failed;

    if (!s_FloorLoaded[0]) { s_FloorLoaded[0] = true; LoadFloor(1, 1); }
    {
        std::lock_guard<std::mutex> lk(s_Mutex);
        auto it = s_PoiLookup.find(poi_id);
        if (it != s_PoiLookup.end()) { r.display = it->second; r.state = State::Resolved; return r; }
    }
    if (!s_FloorLoaded[1]) { s_FloorLoaded[1] = true; LoadFloor(1, 49); }
    {
        std::lock_guard<std::mutex> lk(s_Mutex);
        auto it = s_PoiLookup.find(poi_id);
        if (it != s_PoiLookup.end()) { r.display = it->second; r.state = State::Resolved; return r; }
    }
    if (!s_FloorLoaded[2]) { s_FloorLoaded[2] = true; LoadFloor(2, 1); }
    {
        std::lock_guard<std::mutex> lk(s_Mutex);
        auto it = s_PoiLookup.find(poi_id);
        if (it != s_PoiLookup.end()) { r.display = it->second; r.state = State::Resolved; return r; }
    }
    return r;
}

static std::wstring ApiPath(LinkType type, uint32_t id) {
    std::wstring base;
    switch (type) {
        case LinkType::Item:   base = L"/v2/items/";   break;
        case LinkType::Skill:  base = L"/v2/skills/";  break;
        case LinkType::Trait:  base = L"/v2/traits/";  break;
        case LinkType::Recipe: base = L"/v2/recipes/"; break;
        case LinkType::Skin:   base = L"/v2/skins/";   break;
        case LinkType::Outfit: base = L"/v2/outfits/"; break;
        default: return {};
    }
    return base + std::to_wstring(id);
}

static CachedResult ParseResponse(LinkType type, const std::string& json_str) {
    CachedResult r;
    r.state = State::Failed;
    if (json_str.empty()) return r;
    try {
        auto j = nlohmann::json::parse(json_str);
        if (type == LinkType::Recipe) {
            uint32_t out_id = j.value("output_item_id", 0);
            int count       = j.value("output_item_count", 1);
            r.display = "Recipe";
            if (out_id) r.display += " #" + std::to_string(out_id);
            if (count > 1) r.display += " x" + std::to_string(count);
            r.state = State::Resolved;
            return r;
        }
        std::string name = j.value("name", "");
        if (!name.empty()) {
            r.display = std::move(name);
            r.state   = State::Resolved;
        }
    } catch (...) {}
    return r;
}

static void WorkerThread() {
    while (!s_Shutdown) {
        ApiRequest req;
        {
            std::unique_lock<std::mutex> lock(s_Mutex);
            s_CV.wait(lock, [] { return s_Shutdown || !s_Queue.empty(); });
            if (s_Shutdown && s_Queue.empty()) break;
            req = std::move(s_Queue.front());
            s_Queue.pop();
        }

        CachedResult result;
        if (req.type == LinkType::MapLink) {
            result = FetchMapLink(req.id);
        } else {
            auto path = ApiPath(req.type, req.id);
            result = path.empty() ? CachedResult{}
                                  : ParseResponse(req.type, HttpGet(path));
        }
        if (result.state != State::Resolved) result.state = State::Failed;

        uint64_t key = CacheKey(req.type, req.id);
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Cache[key] = std::move(result);
    }
}

// --- Public API ---
void Initialize() {
    s_Shutdown = false;
    s_Session = WinHttpOpen(
        L"SayAgain/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s_Session) {
        LogF("WinHttpOpen failed (err=%lu)", GetLastError());
    } else {
        s_Connect = WinHttpConnect(s_Session, L"api.guildwars2.com",
                                   INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!s_Connect) LogF("WinHttpConnect failed (err=%lu)", GetLastError());
    }
    s_Worker = std::thread(WorkerThread);
}

void Shutdown() {
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Shutdown = true;
    }
    s_CV.notify_all();
    if (s_Worker.joinable()) s_Worker.join();

    if (s_Connect) { WinHttpCloseHandle(s_Connect); s_Connect = nullptr; }
    if (s_Session) { WinHttpCloseHandle(s_Session); s_Session = nullptr; }

    std::lock_guard<std::mutex> lock(s_Mutex);
    while (!s_Queue.empty()) s_Queue.pop();
    s_Pending.clear();
    s_Cache.clear();
    s_PoiLookup.clear();
    s_FloorLoaded[0] = s_FloorLoaded[1] = s_FloorLoaded[2] = false;
}

std::string LookupOrRequest(LinkType type, uint32_t id) {
    if (type == LinkType::Unknown || type == LinkType::BuildTemplate || id == 0)
        return ChatLinks::GenericLabel(type);

    uint64_t key = CacheKey(type, id);
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = s_Cache.find(key);
        if (it != s_Cache.end()) {
            if (it->second.state == State::Resolved) return it->second.display;
            return ChatLinks::GenericLabel(type);
        }
        if (s_Pending.count(key)) return ChatLinks::GenericLabel(type);
        s_Pending.insert(key);
        s_Queue.push({type, id});
    }
    s_CV.notify_one();
    return ChatLinks::GenericLabel(type);
}

std::string ResolveDisplay(const std::string& text) {
    auto segs = ChatLinks::ParseSegments(text);
    if (segs.empty()) return text;

    std::string out;
    out.reserve(text.size());
    for (const auto& s : segs) {
        if (!s.is_link) {
            out += s.text;
        } else {
            std::string name = LookupOrRequest(s.link.type, s.link.primary_id);
            // Wrap names in brackets for visibility unless the name already starts with one.
            if (!name.empty() && name.front() == '[') out += name;
            else                                       out += "[" + name + "]";
        }
    }
    return out;
}

} // namespace GW2API
