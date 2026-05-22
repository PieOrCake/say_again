#include <windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#include "nexus/Nexus.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include "SharedState.h"
#include "Messages.h"
#include "FloatingIcon.h"
#include "MapData.h"
#include "GW2API.h"

// --- Addon metadata ---
#define V_MAJOR    0
#define V_MINOR    9
#define V_BUILD    0
#define V_REVISION 0

#define TEX_SA_ICON "TEX_SA_ICON"
#include "sa_icon.h"

// --- Global definitions (declared extern in SharedState.h) ---
AddonAPI_t*              APIDefs           = nullptr;
uint32_t                 g_CurrentMapId    = 0;
HWND                     g_GameHandle      = nullptr;
Mumble::Data*            g_MumbleLink      = nullptr;
Settings                 g_Settings;
std::vector<std::string> g_CustomIconNames;

static AddonDefinition_t AddonDef{};

struct MumbleIdentity {
    char     Name[20];
    unsigned Profession;
    unsigned Specialization;
    unsigned Race;
    unsigned MapID;
    unsigned WorldID;
    unsigned TeamColorID;
    bool     IsCommander;
    float    FOV;
    unsigned UISize;
};

// --- GW2 Theme ---
static ImGuiStyle            g_GW2Style;
static std::vector<ImGuiStyle> g_StyleStack;

static void PushGW2Theme() {
    g_StyleStack.push_back(ImGui::GetStyle());
    ImGui::GetStyle() = g_GW2Style;
}

static void PopGW2Theme() {
    if (!g_StyleStack.empty()) {
        ImGui::GetStyle() = g_StyleStack.back();
        g_StyleStack.pop_back();
    }
}

struct ThemeGuard {
    ThemeGuard()  { PushGW2Theme(); }
    ~ThemeGuard() { PopGW2Theme(); }
};

static void BuildGW2Theme() {
    g_GW2Style = ImGui::GetStyle();
    ImGuiStyle& s = g_GW2Style;

    s.WindowRounding    = 6.0f;
    s.ChildRounding     = 4.0f;
    s.FrameRounding     = 4.0f;
    s.PopupRounding     = 4.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 4.0f;

    s.WindowPadding    = ImVec2(10, 10);
    s.FramePadding     = ImVec2(6, 4);
    s.ItemSpacing      = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.ScrollbarSize    = 12.0f;
    s.GrabMinSize      = 8.0f;
    s.WindowBorderSize = 1.0f;
    s.ChildBorderSize  = 1.0f;
    s.PopupBorderSize  = 1.0f;
    s.FrameBorderSize  = 0.0f;
    s.TabBorderSize    = 0.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.07f, 0.07f, 0.09f, 0.80f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);
    c[ImGuiCol_Border]               = ImVec4(0.28f, 0.25f, 0.18f, 0.50f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.14f, 0.13f, 0.11f, 0.80f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.20f, 0.14f, 0.80f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.25f, 0.16f, 0.90f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.09f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.16f, 0.14f, 0.08f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.07f, 0.05f, 0.75f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.12f, 0.11f, 0.09f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.06f, 0.06f, 0.07f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.30f, 0.27f, 0.18f, 0.80f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.36f, 0.22f, 0.90f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.44f, 0.26f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.90f, 0.75f, 0.25f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.85f, 0.70f, 0.25f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.22f, 0.20f, 0.12f, 0.80f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.45f, 0.38f, 0.16f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.18f, 0.16f, 0.10f, 0.70f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.24f, 0.12f, 0.80f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_Separator]            = ImVec4(0.28f, 0.25f, 0.18f, 0.40f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.50f, 0.42f, 0.20f, 0.70f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.65f, 0.55f, 0.25f, 1.00f);
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.27f, 0.18f, 0.30f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.50f, 0.44f, 0.26f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.65f, 0.55f, 0.25f, 0.90f);
    c[ImGuiCol_Tab]                  = ImVec4(0.14f, 0.13f, 0.10f, 0.86f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.35f, 0.30f, 0.14f, 0.90f);
    c[ImGuiCol_TabActive]            = ImVec4(0.28f, 0.24f, 0.10f, 1.00f);
    c[ImGuiCol_TabUnfocused]         = ImVec4(0.10f, 0.09f, 0.07f, 0.97f);
    c[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.18f, 0.16f, 0.10f, 1.00f);
    c[ImGuiCol_Text]                 = ImVec4(0.90f, 0.87f, 0.78f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.47f, 0.40f, 1.00f);
    c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
    c[ImGuiCol_NavHighlight]         = ImVec4(0.70f, 0.58f, 0.20f, 1.00f);
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.13f, 0.10f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.28f, 0.25f, 0.18f, 0.60f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.22f, 0.20f, 0.15f, 0.40f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(0.10f, 0.10f, 0.08f, 0.30f);
    c[ImGuiCol_PlotHistogram]        = ImVec4(0.65f, 0.55f, 0.15f, 1.00f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.68f, 0.20f, 1.00f);
}

static void RenderSectionHeader(const char* label, ImVec4 color) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 left = ImGui::ColorConvertFloat4ToU32(
        ImVec4(color.x * 0.20f, color.y * 0.20f, color.z * 0.20f, 0.50f));
    ImU32 right = IM_COL32(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + w, pos.y + h), left, right, right, left);
    dl->AddLine(ImVec2(pos.x, pos.y + h), ImVec2(pos.x + w * 0.5f, pos.y + h),
        ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 0.30f)), 1.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 4.0f, pos.y + 2.0f));
    ImGui::TextColored(color, "%s", label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + 2.0f));
}

// --- Settings persistence ---
static std::string GetDataDir() {
    const char* dir = APIDefs->Paths_GetAddonDirectory("SayAgain");
    return dir ? dir : "";
}

void ScanIconDir() {
    g_CustomIconNames.clear();
    std::string iconDir = GetDataDir() + "/icons";
    std::filesystem::create_directories(iconDir);
    try {
        for (const auto& entry : std::filesystem::directory_iterator(iconDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            // lowercase extension check
            std::string extL = ext;
            for (char& c : extL) c = (char)tolower((unsigned char)c);
            if (extL != ".png") continue;
            std::string filename = entry.path().filename().string();
            g_CustomIconNames.push_back(filename);
            // Pre-load the texture so it's ready for the dropdown preview
            std::string texId = GetIconTexId(filename);
            std::string fullPath = iconDir + "/" + filename;
            APIDefs->Textures_GetOrCreateFromFile(texId.c_str(), fullPath.c_str());
        }
    } catch (...) {}
    std::sort(g_CustomIconNames.begin(), g_CustomIconNames.end());
}

void SaveSettings() {
    std::string dir = GetDataDir();
    if (dir.empty()) return;
    std::filesystem::create_directories(dir);
    nlohmann::json j;
    j["direct_post"]      = g_Settings.directPost;
    j["close_on_send"]    = g_Settings.closeOnSend;
    j["icon_size"]        = g_Settings.iconSize;
    j["icon_x"]           = g_Settings.iconX;
    j["icon_y"]           = g_Settings.iconY;
    j["icon_locked"]      = g_Settings.iconLocked;
    j["columns"]          = g_Settings.columns;
    j["anim_style"]       = g_Settings.animStyle;
    j["custom_icon_name"]  = g_Settings.customIconName;
    j["multi_line_delay"]  = g_Settings.multiLineDelay;
    j["msg_prefix"]        = g_Settings.messagePrefix;
    std::ofstream f(dir + "/settings.json");
    if (f.is_open()) f << j.dump(2);
}

static void LoadSettings() {
    std::string path = GetDataDir() + "/settings.json";
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("direct_post"))   g_Settings.directPost  = j["direct_post"].get<bool>();
        if (j.contains("close_on_send")) g_Settings.closeOnSend = j["close_on_send"].get<bool>();
        if (j.contains("icon_size"))     g_Settings.iconSize    = j["icon_size"].get<float>();
        if (j.contains("icon_x") && !j["icon_x"].is_null()) g_Settings.iconX = j["icon_x"].get<float>();
        if (j.contains("icon_y") && !j["icon_y"].is_null()) g_Settings.iconY = j["icon_y"].get<float>();
        if (j.contains("icon_locked"))   g_Settings.iconLocked  = j["icon_locked"].get<bool>();
        if (j.contains("columns"))       g_Settings.columns     = j["columns"].get<int>();
        if (j.contains("anim_style"))       g_Settings.animStyle      = j["anim_style"].get<int>();
        if (j.contains("custom_icon_name")) g_Settings.customIconName = j["custom_icon_name"].get<std::string>();
        if (j.contains("multi_line_delay")) g_Settings.multiLineDelay = j["multi_line_delay"].get<int>();
        if (j.contains("msg_prefix"))      g_Settings.messagePrefix  = j["msg_prefix"].get<std::string>();
    } catch (...) {}
}

// --- Keybind + event callbacks ---
static void ProcessKeybind(const char* /*id*/, bool isRelease) {
    if (isRelease) return;
    g_PanelVisible = !g_PanelVisible;
}

static void OnMumbleIdentityUpdated(void* eventArgs) {
    if (!eventArgs) return;
    const MumbleIdentity* id = (const MumbleIdentity*)eventArgs;
    g_CurrentMapId = id->MapID;
}

// --- Decorated message editor ---

struct LineCallbackData {
    int  idx;
    bool wantsEnter;
    int  enterCursorPos;
};

static int LineInputCallback(ImGuiInputTextCallbackData* data) {
    auto* ld = (LineCallbackData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeyPadEnter, false)) {
            ld->wantsEnter     = true;
            ld->enterCursorPos = data->CursorPos;
        }
    }
    return 0;
}

static std::vector<std::string> s_DecLines;
static int s_DecFocusLine = -1;

static void RenderDecoratedEditor(char* buf, size_t bufSize, bool resetState) {
    if (resetState || s_DecLines.empty()) {
        s_DecLines.clear();
        std::string full(buf);
        size_t start = 0, pos;
        while ((pos = full.find('\n', start)) != std::string::npos) {
            s_DecLines.push_back(full.substr(start, pos - start));
            start = pos + 1;
        }
        s_DecLines.push_back(full.substr(start));
        if (s_DecLines.empty()) s_DecLines.push_back("");
        s_DecFocusLine = -1;
    }

    static const ImVec4 kGreen(0.2f, 0.8f, 0.2f, 1.0f);
    static const ImVec4 kRed      (0.55f, 0.10f, 0.10f, 1.0f);
    static const ImVec4 kRedHover (0.75f, 0.15f, 0.15f, 1.0f);
    static const ImVec4 kRedActive(0.90f, 0.20f, 0.20f, 1.0f);

    int pendingEnterIdx    = -1;
    int pendingEnterCursor = 0;
    int pendingDeleteIdx   = -1;

    bool showX = s_DecLines.size() > 1;

    float lineH  = ImGui::GetFrameHeightWithSpacing();
    float textH  = ImGui::GetTextLineHeightWithSpacing();
    float padY   = ImGui::GetStyle().WindowPadding.y * 2.0f;
    int   vis    = std::min((int)s_DecLines.size(), 6);
    // Count how many lines will produce a preview row beneath them.
    int   previewRows = 0;
    for (int i = 0; i < vis; ++i) {
        std::string preview = GW2API::ResolveDisplay(s_DecLines[i]);
        if (preview != s_DecLines[i]) ++previewRows;
    }
    float childH = (float)vis * lineH + (float)previewRows * textH + padY;

    // Pre-calculate widths so the input doesn't fight with the X button
    float arrowW  = ImGui::CalcTextSize(">").x + ImGui::GetStyle().ItemSpacing.x;
    float xBtnW   = showX ? (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) : 0.0f;

    ImGui::BeginChild("##deceditor", ImVec2(400, childH), true);

    float inputW = ImGui::GetContentRegionAvail().x - arrowW - xBtnW;

    for (int i = 0; i < (int)s_DecLines.size(); ++i) {
        ImGui::TextColored(kGreen, ">"); ImGui::SameLine();
        ImGui::SetNextItemWidth(inputW);

        char id[32];
        snprintf(id, sizeof(id), "##saline%d", i);

        char lineBuf[1024] = {};
        strncpy(lineBuf, s_DecLines[i].c_str(), sizeof(lineBuf) - 1);

        LineCallbackData lcd{i, false, 0};

        if (s_DecFocusLine == i) {
            ImGui::SetKeyboardFocusHere();
            s_DecFocusLine = -1;
        }

        ImGui::InputText(id, lineBuf, sizeof(lineBuf),
            ImGuiInputTextFlags_CallbackAlways, LineInputCallback, &lcd);

        s_DecLines[i] = lineBuf;

        if (showX) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        kRed);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kRedHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kRedActive);
            char xId[32]; snprintf(xId, sizeof(xId), "x##del%d", i);
            if (ImGui::Button(xId, ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
                pendingDeleteIdx = i;
            ImGui::PopStyleColor(3);
        }

        if (lcd.wantsEnter && pendingEnterIdx == -1) {
            pendingEnterIdx    = i;
            pendingEnterCursor = lcd.enterCursorPos;
        }

        // Preview line: show resolved chat codes if the line contains any.
        std::string preview = GW2API::ResolveDisplay(s_DecLines[i]);
        if (preview != s_DecLines[i]) {
            ImGui::Dummy(ImVec2(arrowW, 0)); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::TextWrapped("%s", preview.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();

    // Split line at cursor on Enter
    if (pendingEnterIdx != -1) {
        int i = pendingEnterIdx;
        std::string tail = s_DecLines[i].substr(pendingEnterCursor);
        s_DecLines[i]    = s_DecLines[i].substr(0, pendingEnterCursor);
        s_DecLines.insert(s_DecLines.begin() + i + 1, tail);
        s_DecFocusLine = i + 1;
    }
    // Delete line via X button (keep at least one line)
    else if (pendingDeleteIdx != -1 && (int)s_DecLines.size() > 1) {
        int i = pendingDeleteIdx;
        s_DecLines.erase(s_DecLines.begin() + i);
        s_DecFocusLine = std::max(0, i - 1);
    }

    // Reconstruct buf
    std::string result;
    for (int i = 0; i < (int)s_DecLines.size(); ++i) {
        if (i > 0) result += '\n';
        result += s_DecLines[i];
    }
    strncpy(buf, result.c_str(), bufSize - 1);
    buf[bufSize - 1] = '\0';
}

// --- Render callbacks ---
static void AddonRender() {
    ThemeGuard tg;
    TickImportModal();
    RenderImportModal();
    RenderFloatingIcon();
}

static void AddonOptions() {
    ThemeGuard tg;

    static const ImVec4 kGold(0.70f, 0.58f, 0.20f, 1.0f);
    bool dirty = false;

    RenderSectionHeader("Behaviour", kGold);
    dirty |= ImGui::Checkbox("Direct post", &g_Settings.directPost);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: clicking a message opens chat and sends it automatically.\nOff: the message is copied to clipboard only.");
    dirty |= ImGui::Checkbox("Close panel after sending", &g_Settings.closeOnSend);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Right-click the floating icon to toggle this on the fly.");
    if (ImGui::SliderInt("Delay between lines##ml", &g_Settings.multiLineDelay, 100, 3000, "%dms")) {
        g_Settings.multiLineDelay = ((g_Settings.multiLineDelay + 25) / 50) * 50;
        dirty = true;
    }
    {
        char prefixBuf[32];
        strncpy(prefixBuf, g_Settings.messagePrefix.c_str(), sizeof(prefixBuf) - 1);
        prefixBuf[sizeof(prefixBuf) - 1] = '\0';
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("Message prefix##pfx", prefixBuf, sizeof(prefixBuf))) {
            g_Settings.messagePrefix = prefixBuf;
            dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Prepended to every sent line, after the channel command.\nLeave blank for no prefix.");
    }
    ImGui::Spacing();
    RenderSectionHeader("Appearance", kGold);

    // Icon selection dropdown
    {
        const std::string& cur = g_Settings.customIconName;
        const char* preview = cur.empty() ? "Default" : cur.c_str();
        if (ImGui::BeginCombo("Icon", preview)) {
            // Default (embedded)
            if (ImGui::Selectable("Default", cur.empty())) {
                g_Settings.customIconName.clear();
                dirty = true;
            }
            if (cur.empty()) ImGui::SetItemDefaultFocus();
            // Custom icons from icons/ dir
            for (const auto& name : g_CustomIconNames) {
                bool selected = (cur == name);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    g_Settings.customIconName = name;
                    dirty = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            ScanIconDir();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Place .png files in the SayAgain/icons/ folder\ninside your Nexus addon directory.");
    }

    dirty |= ImGui::SliderFloat("Icon size", &g_Settings.iconSize, 16.0f, 128.0f);
    dirty |= ImGui::Checkbox("Lock icon position", &g_Settings.iconLocked);
    if (g_Settings.iconLocked) {
        ImGui::Text("Position: %.0f, %.0f", g_Settings.iconX, g_Settings.iconY);
    }
    if (ImGui::Button("Reset to default position")) {
        g_Settings.iconX = -9999.0f;
        g_Settings.iconY = -9999.0f;
        FloatingIcon_ForceReposition();
        g_Settings.iconLocked = false;
        dirty = true;
    }
    dirty |= ImGui::SliderInt("Grid columns", &g_Settings.columns, 1, 6);
    const char* animItems[] = { "Fade", "Slide", "Pop" };
    dirty |= ImGui::Combo("Animation", &g_Settings.animStyle, animItems, 3);

    ImGui::Spacing();
    RenderSectionHeader("Messages", kGold);

    static int  s_EditIdx    = -1;
    static bool s_OpenEditor = false;
    static ChatMessage s_EditBuf;
    static int  s_DeleteIdx     = -1;
    static bool s_DeletePending = false;
    static ChatMessage s_DeleteMsg;
    static int  s_OpenAll = 0; // 1=expand, -1=collapse, 0=no-op
    static int  s_LastOptionsFrame = -1000;
    static int                 s_DragSrcIdx  = -1;
    static uint32_t            s_DragScopeId = 0;
    static std::vector<ImVec2> s_DragRowMins;
    static std::vector<ImVec2> s_DragRowMaxs;
    static std::vector<int>    s_DragRowMsg;

    int  curFrame  = ImGui::GetFrameCount();
    bool justOpened = (curFrame - s_LastOptionsFrame) > 2;
    s_LastOptionsFrame = curFrame;
    uint32_t normCurMap = (g_CurrentMapId != 0) ? MapData::NormalizeMapId(g_CurrentMapId) : 0;

    if (ImGui::Button("Add Message")) {
        s_EditIdx = -1;
        s_EditBuf = {};
        s_OpenEditor = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Export")) ExportMessages();
    ImGui::SameLine();
    if (ImGui::Button("Import")) ImportMessages();
    if (ChatShortsAvailable()) {
        ImGui::SameLine();
        if (ImGui::Button("Import from Chat Shorts")) ImportFromChatShorts();
    }

    auto renderRow = [&](int i, uint32_t scopeId) {
        auto& msg = g_Messages[i];
        ImGui::PushID(i * 100000 + (int)scopeId);

        ImVec2 rowStart = ImGui::GetCursorScreenPos();
        float  rowW     = ImGui::GetContentRegionAvail().x;

        // Full-row selectable as drag source
        ImGui::Selectable("##drag", false, ImGuiSelectableFlags_AllowItemOverlap, ImVec2(rowW, 0));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            s_DragSrcIdx  = i;
            s_DragScopeId = scopeId;
            ImGui::SetDragDropPayload("SA_MSG_ROW", &i, sizeof(int));
            ImGui::Text("%s", msg.shortLabel.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SetCursorScreenPos(rowStart);

        ImGui::Text("%s", msg.shortLabel.c_str());
        ImGui::SameLine();
        std::string preview = GW2API::ResolveDisplay(msg.fullText);
        ImGui::TextDisabled("%.40s%s", preview.c_str(),
            preview.size() > 40 ? "..." : "");
        ImGui::SameLine();
        if (ImGui::SmallButton("Edit")) {
            s_EditIdx    = i;
            s_EditBuf    = msg;
            s_OpenEditor = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Del")) {
            s_DeleteIdx     = i;
            s_DeleteMsg     = msg;
            s_DeletePending = true;
        }

        s_DragRowMins.push_back(rowStart);
        s_DragRowMaxs.push_back(ImVec2(rowStart.x + rowW, rowStart.y + ImGui::GetFrameHeightWithSpacing()));
        s_DragRowMsg.push_back(i);

        ImGui::PopID();
    };

    auto processDropTarget = [&](uint32_t scopeId) {
        if (s_DragSrcIdx < 0 || s_DragScopeId != scopeId || s_DragRowMins.empty()) return;

        ImVec2 mouse      = ImGui::GetMousePos();
        int    dropVisIdx = -1;
        bool   dropAfter  = false;

        for (size_t vi = 0; vi < s_DragRowMins.size(); vi++) {
            if (mouse.y >= s_DragRowMins[vi].y && mouse.y < s_DragRowMaxs[vi].y) {
                dropVisIdx = (int)vi;
                dropAfter  = mouse.y >= (s_DragRowMins[vi].y + s_DragRowMaxs[vi].y) * 0.5f;
                break;
            }
        }
        if (dropVisIdx < 0 && mouse.y >= s_DragRowMaxs.back().y)
            { dropVisIdx = (int)s_DragRowMins.size() - 1; dropAfter = true; }

        if (dropVisIdx >= 0) {
            float lineY = dropAfter ? s_DragRowMaxs[dropVisIdx].y : s_DragRowMins[dropVisIdx].y;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(s_DragRowMins[dropVisIdx].x, lineY),
                ImVec2(s_DragRowMaxs[dropVisIdx].x, lineY),
                IM_COL32(220, 190, 80, 255), 2.0f);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (dropVisIdx >= 0) {
                int hovered  = s_DragRowMsg[dropVisIdx];
                int from     = s_DragSrcIdx;
                int insertAt;
                if (dropAfter)
                    insertAt = (from <= hovered) ? hovered : hovered + 1;
                else
                    insertAt = (from < hovered)  ? hovered - 1 : hovered;
                insertAt = std::max(0, std::min(insertAt, (int)g_Messages.size() - 1));
                if (from != insertAt) {
                    MoveMessage(from, insertAt);
                    dirty = true;
                }
            }
            s_DragSrcIdx = -1;
        }
    };

    if (ImGui::SmallButton("Expand All"))   s_OpenAll =  1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse All")) s_OpenAll = -1;

    auto applyOpen = [&]() {
        if (s_OpenAll != 0) ImGui::SetNextItemOpen(s_OpenAll > 0, ImGuiCond_Always);
    };
    auto mapHasMessages = [&](uint32_t mid) {
        for (const auto& m : g_Messages)
            if (std::find(m.mapIds.begin(), m.mapIds.end(), mid) != m.mapIds.end()) return true;
        return false;
    };

    s_DragRowMins.clear(); s_DragRowMaxs.clear(); s_DragRowMsg.clear();
    applyOpen();
    if (ImGui::CollapsingHeader("All Maps##sayagain_hdr")) {
        for (int i = 0; i < (int)g_Messages.size(); ++i) {
            if (!g_Messages[i].mapIds.empty()) continue;
            renderRow(i, 0);
        }
        processDropTarget(0);
    }

    for (const auto& grp : MapData::GetMapGroups()) {
        bool grpVisible = false;
        for (const auto& entry : grp.maps)
            if (mapHasMessages(entry.id)) { grpVisible = true; break; }
        if (!grpVisible) continue;

        if (justOpened && normCurMap != 0 && mapHasMessages(normCurMap)) {
            for (const auto& entry : grp.maps)
                if (entry.id == normCurMap) { ImGui::SetNextItemOpen(true, ImGuiCond_Always); break; }
        }
        applyOpen();
        ImGui::PushID(grp.groupName);
        if (ImGui::CollapsingHeader(grp.groupName)) {
            ImGui::Indent();
            for (const auto& entry : grp.maps) {
                if (!mapHasMessages(entry.id)) continue;
                if (justOpened && entry.id == normCurMap)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                applyOpen();
                s_DragRowMins.clear(); s_DragRowMaxs.clear(); s_DragRowMsg.clear();
                ImGui::PushID((int)entry.id);
                if (ImGui::CollapsingHeader(entry.name)) {
                    for (int i = 0; i < (int)g_Messages.size(); ++i) {
                        auto& m = g_Messages[i];
                        if (std::find(m.mapIds.begin(), m.mapIds.end(), entry.id) == m.mapIds.end()) continue;
                        renderRow(i, entry.id);
                    }
                    processDropTarget(entry.id);
                }
                ImGui::PopID();
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }

    s_OpenAll = 0;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s_DragSrcIdx >= 0)
        s_DragSrcIdx = -1;

    // --- Delete confirmation modal ---
    if (s_DeletePending) {
        ImGui::OpenPopup("Delete Message##sayagain");
        s_DeletePending = false;
    }
    if (ImGui::BeginPopupModal("Delete Message##sayagain", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"?", s_DeleteMsg.shortLabel.c_str());
        if (s_DeleteMsg.mapIds.size() > 1) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f),
                "Note: this message appears on %d maps:", (int)s_DeleteMsg.mapIds.size());
            for (uint32_t mid : s_DeleteMsg.mapIds)
                ImGui::BulletText("%s", MapData::GetMapName(mid).c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Delete")) {
            if (s_DeleteIdx >= 0 && s_DeleteIdx < (int)g_Messages.size())
                g_Messages.erase(g_Messages.begin() + s_DeleteIdx);
            SaveMessages();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Message editor modal ---
    if (s_OpenEditor) {
        ImGui::OpenPopup("Edit Message##sayagain");
        s_OpenEditor = false;
    }

    if (ImGui::BeginPopupModal("Edit Message##sayagain", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        static char labelBuf[64]  = {};
        static char textBuf[1024] = {};
        static char mapFilter[64] = {};
        static bool allMaps       = true;
        static std::vector<uint32_t> selectedMaps;

        if (ImGui::IsWindowAppearing()) {
            strncpy(labelBuf, s_EditBuf.shortLabel.c_str(), sizeof(labelBuf)-1);
            strncpy(textBuf,  s_EditBuf.fullText.c_str(),   sizeof(textBuf)-1);
            allMaps      = s_EditBuf.mapIds.empty();
            selectedMaps = s_EditBuf.mapIds;
            mapFilter[0] = '\0';
        }

        bool decReset = ImGui::IsWindowAppearing();

        ImGui::InputText("Short label", labelBuf, sizeof(labelBuf));
        if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Tab, false))
            s_DecFocusLine = 0;
        RenderDecoratedEditor(textBuf, sizeof(textBuf), decReset);
        ImGui::TextDisabled("Tip: newlines send each line as a separate message (party/squad only).");

        bool wasAllMaps = allMaps;
        ImGui::Checkbox("All Maps", &allMaps);
        if (allMaps && !wasAllMaps) selectedMaps.clear();

        if (!allMaps) {
            ImGui::InputText("Filter maps", mapFilter, sizeof(mapFilter));
            ImGui::BeginChild("##maplist", ImVec2(400, 200), true);
            std::string filterStr(mapFilter);
            for (char& ch : filterStr) ch = (char)tolower((unsigned char)ch);

            for (const auto& grp : MapData::GetMapGroups()) {
                // Check if any map in group passes filter and is unselected
                bool anyVisible = false;
                for (const auto& e : grp.maps) {
                    bool already = false;
                    for (uint32_t sel : selectedMaps) if (sel == e.id) { already = true; break; }
                    if (already) continue;
                    if (!filterStr.empty()) {
                        std::string ln = e.name;
                        for (char& ch : ln) ch = (char)tolower((unsigned char)ch);
                        if (ln.find(filterStr) == std::string::npos) continue;
                    }
                    anyVisible = true;
                    break;
                }
                if (!anyVisible) continue;

                // Collapsed by default — open when filter is active
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen * (!filterStr.empty());
                if (!filterStr.empty()) flags = ImGuiTreeNodeFlags_DefaultOpen;
                bool open = ImGui::TreeNodeEx(grp.groupName, flags);
                if (open) {
                    for (const auto& e : grp.maps) {
                        bool already = false;
                        for (uint32_t sel : selectedMaps) if (sel == e.id) { already = true; break; }
                        if (already) continue;
                        if (!filterStr.empty()) {
                            std::string ln = e.name;
                            for (char& ch : ln) ch = (char)tolower((unsigned char)ch);
                            if (ln.find(filterStr) == std::string::npos) continue;
                        }
                        if (ImGui::Selectable(e.name)) {
                            selectedMaps.push_back(e.id);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();

            // Add current map button
            if (g_CurrentMapId != 0) {
                uint32_t normCur = MapData::NormalizeMapId(g_CurrentMapId);
                bool alreadySel = false;
                for (uint32_t sel : selectedMaps)
                    if (MapData::NormalizeMapId(sel) == normCur) { alreadySel = true; break; }
                if (!alreadySel) {
                    if (ImGui::Button("Add current map")) {
                        selectedMaps.push_back(normCur);
                    }
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("(%s)", MapData::GetMapName(normCur).c_str());
            }

            // Chips for selected maps
            for (int k = 0; k < (int)selectedMaps.size(); ++k) {
                ImGui::PushID(k);
                std::string chip = MapData::GetMapName(selectedMaps[k]) + " [x]";
                if (ImGui::SmallButton(chip.c_str())) {
                    selectedMaps.erase(selectedMaps.begin() + k);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (!selectedMaps.empty()) ImGui::NewLine();
        }

        ImGui::Spacing();
        if (ImGui::Button("OK")) {
            ChatMessage m;
            m.shortLabel = labelBuf;
            m.fullText   = textBuf;
            m.mapIds     = allMaps ? std::vector<uint32_t>{} : selectedMaps;

            if (s_EditIdx >= 0 && s_EditIdx < (int)g_Messages.size()) {
                g_Messages[s_EditIdx] = m;
            } else {
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

    if (dirty) SaveSettings();
}

// --- Lifecycle ---
void AddonLoad(AddonAPI_t* aApi) {
    APIDefs = aApi;
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))APIDefs->ImguiMalloc,
                                 (void(*)(void*, void*))APIDefs->ImguiFree);

    BuildGW2Theme();

    g_MumbleLink = (Mumble::Data*)APIDefs->DataLink_Get(DL_MUMBLE_LINK);

    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", OnMumbleIdentityUpdated);
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->InputBinds_RegisterWithString("KB_SAY_AGAIN_TOGGLE", ProcessKeybind, "(null)");
    APIDefs->Textures_LoadFromMemory(TEX_SA_ICON, (void*)SA_ICON, SA_ICON_len, nullptr);

    LoadSettings();
    LoadMessages();
    ScanIconDir();
    GW2API::Initialize();

    APIDefs->Log(LOGL_INFO, "SayAgain", "Addon loaded");
}

void AddonUnload() {
    FloatingIcon_Shutdown();
    GW2API::Shutdown();
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", OnMumbleIdentityUpdated);
    APIDefs->InputBinds_Deregister("KB_SAY_AGAIN_TOGGLE");
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->GUI_Deregister(AddonRender);

    SaveSettings();
    SaveMessages();

    g_MumbleLink = nullptr;
    g_GameHandle = nullptr;
    APIDefs      = nullptr;
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD /*ul_reason*/, LPVOID /*lpReserved*/) {
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() {
    AddonDef.Signature  = 0x5A3A1D07;
    AddonDef.APIVersion = NEXUS_API_VERSION;
    AddonDef.Name       = "Say Again";
    AddonDef.Version.Major    = V_MAJOR;
    AddonDef.Version.Minor    = V_MINOR;
    AddonDef.Version.Build    = V_BUILD;
    AddonDef.Version.Revision = V_REVISION;
    AddonDef.Author      = "PieOrCake.7635";
    AddonDef.Description = "Save and post common chat messages via a floating icon and button grid.";
    AddonDef.Load        = AddonLoad;
    AddonDef.Unload      = AddonUnload;
    AddonDef.Flags       = AF_None;
    AddonDef.Provider    = UP_GitHub;
    AddonDef.UpdateLink  = "https://github.com/PieOrCake/say_again";
    return &AddonDef;
}
