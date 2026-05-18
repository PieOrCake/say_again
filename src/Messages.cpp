#include "Messages.h"
#include <windows.h>
#include "imgui.h"
#include <commdlg.h>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "nexus/Nexus.h"

extern AddonAPI_t* APIDefs;

std::vector<ChatMessage> g_Messages;

static std::string GetMessagesPath() {
    const char* dir = APIDefs->Paths_GetAddonDirectory("SayAgain");
    if (!dir) return "";
    std::filesystem::create_directories(dir);
    return std::string(dir) + "/messages.json";
}

static nlohmann::json MessageToJson(const ChatMessage& m) {
    nlohmann::json j;
    j["label"]   = m.shortLabel;
    j["text"]    = m.fullText;
    j["map_ids"] = m.mapIds;
    return j;
}

static ChatMessage MessageFromJson(const nlohmann::json& j) {
    ChatMessage m;
    m.shortLabel = j.value("label", "");
    m.fullText   = j.value("text",  "");
    if (j.contains("map_ids") && j["map_ids"].is_array())
        m.mapIds = j["map_ids"].get<std::vector<uint32_t>>();
    return m;
}

void LoadMessages() {
    std::string path = GetMessagesPath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        g_Messages.clear();
        for (const auto& item : j) {
            g_Messages.push_back(MessageFromJson(item));
        }
        // Enforce ordering invariant: All Maps messages at front
        std::stable_partition(g_Messages.begin(), g_Messages.end(),
            [](const ChatMessage& m) { return m.mapIds.empty(); });
    } catch (...) {}
}

void SaveMessages() {
    std::string path = GetMessagesPath();
    if (path.empty()) return;
    nlohmann::json j = nlohmann::json::array();
    for (const auto& m : g_Messages)
        j.push_back(MessageToJson(m));
    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2);
}

// --- File dialog helpers ---
static std::string OpenFileDialog(bool save) {
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = sizeof(buf);
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "json";
    if (save) {
        ofn.Flags = OFN_OVERWRITEPROMPT;
        return GetSaveFileNameA(&ofn) ? buf : "";
    } else {
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        return GetOpenFileNameA(&ofn) ? buf : "";
    }
}

// Pending import decision state (shown next frame as a modal)
static bool s_ImportPending = false;
static std::vector<ChatMessage> s_ImportBuf;

void ImportMessages() {
    std::string path = OpenFileDialog(false);
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        s_ImportBuf.clear();
        for (const auto& item : j)
            s_ImportBuf.push_back(MessageFromJson(item));
        s_ImportPending = true;
    } catch (...) {}
}

void ExportMessages() {
    std::string path = OpenFileDialog(true);
    if (path.empty()) return;
    nlohmann::json j = nlohmann::json::array();
    for (const auto& m : g_Messages)
        j.push_back(MessageToJson(m));
    std::ofstream f(path);
    if (f.is_open()) f << j.dump(2);
}

// --- Chat Shorts import ---

static std::string GetChatShortsPath() {
    const char* dir = APIDefs->Paths_GetAddonDirectory("SayAgain");
    if (!dir) return "";
    return (std::filesystem::path(dir).parent_path() / "chat_shorts" / "settings.json").string();
}

bool ChatShortsAvailable() {
    std::string path = GetChatShortsPath();
    return !path.empty() && std::filesystem::exists(path);
}

void ImportFromChatShorts() {
    std::string path = GetChatShortsPath();
    if (path.empty()) return;
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        s_ImportBuf.clear();
        for (const auto& entry : j["chat_messages"]) {
            uint32_t mapId = entry[0].get<uint32_t>();
            for (const auto& msg : entry[1]) {
                ChatMessage m;
                m.shortLabel = msg.value("short_message", "");
                m.fullText   = msg.value("message", "");
                if (mapId != 0) m.mapIds = {mapId};
                s_ImportBuf.push_back(m);
            }
        }
        if (!s_ImportBuf.empty()) s_ImportPending = true;
    } catch (...) {}
}

// Call each frame from the render callback to show import decision modal
void TickImportModal() {
    if (!s_ImportPending) return;
    ImGui::OpenPopup("Import Messages##sayagain");
    s_ImportPending = false;
}

void RenderImportModal() {
    if (ImGui::BeginPopupModal("Import Messages##sayagain", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Import %d message(s). Replace existing list or append?",
            (int)s_ImportBuf.size());
        ImGui::Spacing();
        if (ImGui::Button("Replace")) {
            g_Messages = s_ImportBuf;
            std::stable_partition(g_Messages.begin(), g_Messages.end(),
                [](const ChatMessage& m) { return m.mapIds.empty(); });
            SaveMessages();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Append")) {
            for (const auto& m : s_ImportBuf) {
                bool dupe = false;
                for (const auto& existing : g_Messages)
                    if (existing.fullText == m.fullText) { dupe = true; break; }
                if (dupe) continue;
                if (m.mapIds.empty()) {
                    auto it = g_Messages.begin();
                    while (it != g_Messages.end() && it->mapIds.empty()) ++it;
                    g_Messages.insert(it, m);
                } else {
                    g_Messages.push_back(m);
                }
            }
            SaveMessages();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void MoveMessage(int fromIdx, int toIdx) {
    if (fromIdx < 0 || toIdx < 0 ||
        fromIdx >= (int)g_Messages.size() ||
        toIdx   >= (int)g_Messages.size() ||
        fromIdx == toIdx) return;
    ChatMessage msg = std::move(g_Messages[fromIdx]);
    g_Messages.erase(g_Messages.begin() + fromIdx);
    g_Messages.insert(g_Messages.begin() + toIdx, std::move(msg));
    SaveMessages();
}
