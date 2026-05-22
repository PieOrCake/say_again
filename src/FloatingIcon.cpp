#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <sstream>
#include <random>
#include "imgui.h"
#include "imgui_internal.h"
#include "SharedState.h"
#include "Messages.h"
#include "MapData.h"
#include "GW2API.h"

#define TEX_SA_ICON "TEX_SA_ICON"

bool  g_PanelVisible = false;
float g_AnimProgress = 0.0f;

static float       s_FlashTimer = 0.0f;
static unsigned    s_LastUITick      = 0;
static float       s_TickStaleTimer  = 0.0f;  // seconds tick has been unchanged
static bool        s_IsLoading       = false;

// --- Unload guard for background threads ---
static std::atomic<bool> g_Unloading{false};
static std::mutex        g_SendMutex;

void FloatingIcon_Shutdown() {
    g_Unloading = true;
}

// --- Helpers ---
static std::string ApplyRandomWords(const std::string& text) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::string result;
    result.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                std::string group = text.substr(i + 1, close - i - 1);
                if (group.find('|') != std::string::npos) {
                    std::vector<std::string> choices;
                    std::istringstream ss(group);
                    std::string token;
                    while (std::getline(ss, token, '|'))
                        if (!token.empty()) choices.push_back(token);
                    if (choices.empty()) {
                        result += '{';
                        i++;
                        continue;
                    }
                    std::uniform_int_distribution<size_t> dist(0, choices.size() - 1);
                    result += choices[dist(rng)];
                    i = close + 1;
                    continue;
                }
            }
        }
        result += text[i++];
    }
    return result;
}

static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
        if (!line.empty()) out.push_back(line);
    return out;
}

// --- Chat posting (Mission Finder method) ---
static LPARAM MakeLParam(uint32_t vk, bool down) {
    int64_t lp = !down;
    lp = lp << 1;
    lp += !down;
    lp = lp << 1;
    lp += 0;
    lp = lp << 1;
    lp = lp << 4;
    lp = lp << 1;
    lp = lp << 8;
    lp += MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    lp = lp << 16;
    lp += 1;
    return (LPARAM)lp;
}

static bool CopyToClipboardUnicode(HWND hwnd, const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (wlen <= 0) return false;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * (wlen + 1));
    if (!hMem) return false;
    WCHAR* buf = (WCHAR*)GlobalLock(hMem);
    if (!buf) { GlobalFree(hMem); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), buf, wlen);
    buf[wlen] = L'\0';
    GlobalUnlock(hMem);
    if (!OpenClipboard(hwnd)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    return true;
}

static void PasteAndSendFromThread(std::string message) {
    HWND game = g_GameHandle;
    if (!game) {
        game = FindWindowA("ArenaNet_Dx_Window_Class", nullptr);
        if (!game) game = FindWindowA("ArenaNet_Gr_Window_Class", nullptr);
        if (!game) return;
        g_GameHandle = game;
    }

    if (!CopyToClipboardUnicode(game, message)) return;

    constexpr int kDelay = 50;

    // Open chat if not already focused. While mounted (e.g. Skyscale), GW2 may consume the
    // first Enter as a mount action, so we retry and poll IsTextboxFocused for confirmation.
    bool focused = g_MumbleLink && g_MumbleLink->Context.IsTextboxFocused;
    if (!focused) {
        bool opened = false;
        for (int attempt = 0; attempt < 3 && !g_Unloading && !opened; ++attempt) {
            SendMessage(game, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
            SendMessage(game, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
            for (int poll = 0; poll < 4 && !g_Unloading; ++poll) {
                Sleep(kDelay);
                if (g_MumbleLink && g_MumbleLink->Context.IsTextboxFocused) {
                    opened = true;
                    break;
                }
            }
        }
    }

    if (g_Unloading) return;

    // Ctrl down (SendInput — GW2 needs modifier at hardware level)
    INPUT ctrlDown{};
    ctrlDown.type = INPUT_KEYBOARD;
    ctrlDown.ki.wVk   = VK_CONTROL;
    ctrlDown.ki.wScan = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    SendInput(1, &ctrlDown, sizeof(INPUT));
    Sleep(kDelay);

    if (g_Unloading) return;

    // Ctrl+V via WM_KEYDOWN (GW2 processes these for the chat box)
    SendMessage(game, WM_KEYDOWN, 'V', MakeLParam('V', true));
    SendMessage(game, WM_KEYUP,   'V', MakeLParam('V', false));
    Sleep(kDelay);

    // Ctrl up
    INPUT ctrlUp{};
    ctrlUp.type = INPUT_KEYBOARD;
    ctrlUp.ki.wVk    = VK_CONTROL;
    ctrlUp.ki.wScan  = (WORD)MapVirtualKeyA(VK_CONTROL, MAPVK_VK_TO_VSC);
    ctrlUp.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &ctrlUp, sizeof(INPUT));
    Sleep(kDelay);

    if (g_Unloading) return;

    // Enter to send
    SendMessage(game, WM_KEYDOWN, VK_RETURN, MakeLParam(VK_RETURN, true));
    SendMessage(game, WM_KEYUP,   VK_RETURN, MakeLParam(VK_RETURN, false));
}

static void PostMessage(const ChatMessage& msg, const char* channelPrefix) {
    std::string resolved = ApplyRandomWords(msg.fullText);
    if (g_Settings.directPost) {
        auto lines = SplitLines(resolved);
        int delay = g_Settings.multiLineDelay;
        std::string prefix(channelPrefix);
        std::string msgPrefix = g_Settings.messagePrefix.empty() ? "" : g_Settings.messagePrefix + " ";
        std::thread([lines, prefix, msgPrefix, delay]() {
            std::lock_guard<std::mutex> lock(g_SendMutex);
            for (size_t i = 0; i < lines.size(); ++i) {
                if (g_Unloading) break;
                PasteAndSendFromThread(prefix + msgPrefix + lines[i]);
                if (i + 1 < lines.size())
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }).detach();
    } else {
        std::string msgPrefix = g_Settings.messagePrefix.empty() ? "" : g_Settings.messagePrefix + " ";
        std::string text = channelPrefix + msgPrefix + resolved;
        CopyToClipboardUnicode(g_GameHandle, text);
        APIDefs->GUI_SendAlert("Copied to clipboard!");
    }

    if (g_Settings.closeOnSend)
        g_PanelVisible = false;
}

// --- Animation ---
static float AnimLerp(float current, float target, float dt) {
    float diff = target - current;
    if (fabsf(diff) < 0.001f) return target;
    return current + diff * (1.0f - expf(-8.0f * dt));
}

static float PopScale(float t) {
    return t * (1.0f + 0.15f * sinf(t * 3.14159f * 2.0f) * (1.0f - t));
}

// Set to true one frame after a position reset to force ImGui to reposition the window
static bool s_ForceReposition = true;

void FloatingIcon_ForceReposition() { s_ForceReposition = true; }

// --- Main render ---
void RenderFloatingIcon() {
    if (!APIDefs) return;

    if (g_MumbleLink) {
        float dt = ImGui::GetIO().DeltaTime;
        if (g_MumbleLink->UITick == s_LastUITick) {
            s_TickStaleTimer += dt;
        } else {
            s_LastUITick     = g_MumbleLink->UITick;
            s_TickStaleTimer = 0.0f;
        }
        s_IsLoading = (s_TickStaleTimer > 1.0f);

        if (s_IsLoading || g_MumbleLink->Context.IsMapOpen) {
            g_PanelVisible = false;
            g_AnimProgress = 0.0f;
            return;
        }
    }

    NexusLinkData_t* nxl = (NexusLinkData_t*)APIDefs->DataLink_Get(DL_NEXUS_LINK);
    float screenW = nxl ? (float)nxl->Width  : 1920.0f;
    float screenH = nxl ? (float)nxl->Height : 1080.0f;

    float sz = g_Settings.iconSize;

    // Apply default bottom-left position if never set
    if (g_Settings.iconX < -1000.0f) g_Settings.iconX = 20.0f;
    if (g_Settings.iconY < -1000.0f) g_Settings.iconY = screenH - sz - 20.0f;

    float& ix = g_Settings.iconX;
    float& iy = g_Settings.iconY;

    // --- Floating icon window ---
    ImGuiWindowFlags iconFlags =
        ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoResize      |
        ImGuiWindowFlags_NoScrollbar   | ImGuiWindowFlags_NoDecoration  |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        (g_Settings.iconLocked ? ImGuiWindowFlags_NoMove : 0);

    // Set position only on first use or after an explicit reset
    if (s_ForceReposition) {
        ImGui::SetNextWindowPos(ImVec2(ix, iy), ImGuiCond_Always);
        s_ForceReposition = false;
    }
    ImGui::SetNextWindowSize(ImVec2(sz, sz), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##sa_icon", nullptr, iconFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        std::string texId = GetIconTexId(g_Settings.customIconName);
        Texture_t* s_Tex = APIDefs->Textures_Get(texId.c_str());

        float m = 2.0f;
        ImVec2 p0(wp.x + m, wp.y + m);
        ImVec2 p1(wp.x + sz - m, wp.y + sz - m);

        if (s_Tex && s_Tex->Resource) {
            dl->AddImage((ImTextureID)s_Tex->Resource, p0, p1);
        } else {
            dl->AddRectFilled(p0, p1, IM_COL32(80, 70, 40, 200), 6.0f);
            dl->AddText(ImVec2(wp.x + sz * 0.3f, wp.y + sz * 0.35f),
                        IM_COL32(220, 190, 80, 255), "SA");
        }

        // Flash overlay: brief gold pulse on right-click toggle
        if (s_FlashTimer > 0.0f) {
            s_FlashTimer -= ImGui::GetIO().DeltaTime;
            if (s_FlashTimer < 0.0f) s_FlashTimer = 0.0f;
            int flashAlpha = (int)(s_FlashTimer / 0.4f * 100.0f);
            ImVec2 fc(wp.x + sz * 0.5f, wp.y + sz * 0.5f);
            dl->AddCircleFilled(fc, sz * 0.5f - 1.0f, IM_COL32(220, 190, 80, flashAlpha));
        }

        // Persistent gold border when pinned (closeOnSend OFF)
        if (!g_Settings.closeOnSend) {
            ImVec2 bc(wp.x + sz * 0.5f, wp.y + sz * 0.5f);
            dl->AddCircle(bc, sz * 0.5f - 1.0f, IM_COL32(220, 190, 80, 255), 0, 2.0f);
        }

        // When locked, overlay an InvisibleButton to capture left and right clicks
        if (g_Settings.iconLocked) {
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::InvisibleButton("##icon_btn", ImVec2(sz, sz),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                g_PanelVisible = !g_PanelVisible;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                g_Settings.closeOnSend = !g_Settings.closeOnSend;
                SaveSettings();
                s_FlashTimer = 0.4f;
            }
        }

        // Read back position after ImGui handles any dragging natively
        ImVec2 pos = ImGui::GetWindowPos();
        if (pos.x != ix || pos.y != iy) {
            ix = pos.x;
            iy = pos.y;
            SaveSettings();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // --- Animate ---
    float target = g_PanelVisible ? 1.0f : 0.0f;
    g_AnimProgress = AnimLerp(g_AnimProgress, target, ImGui::GetIO().DeltaTime);
    if (g_AnimProgress < 0.01f) return;

    // --- Filter visible messages (split by scope) ---
    std::vector<const ChatMessage*> allMapsVis, curMapVis;
    for (const auto& m : g_Messages) {
        if (m.mapIds.empty()) {
            allMapsVis.push_back(&m);
        } else {
            uint32_t normCur = MapData::NormalizeMapId(g_CurrentMapId);
            for (uint32_t id : m.mapIds)
                if (MapData::NormalizeMapId(id) == normCur) { curMapVis.push_back(&m); break; }
        }
    }
    if (allMapsVis.empty() && curMapVis.empty()) return;

    // Unified list for popup index lookups: all-maps first, then current-map
    std::vector<const ChatMessage*> visible;
    visible.insert(visible.end(), allMapsVis.begin(), allMapsVis.end());
    visible.insert(visible.end(), curMapVis.begin(),  curMapVis.end());
    int allMapsCount = (int)allMapsVis.size();

    // --- Grid panel size & position ---
    int   cols   = std::max(1, g_Settings.columns);
    float btnW   = 120.0f;
    float btnH   = 30.0f;
    float headerH = 18.0f;
    float allMapsRows = allMapsVis.empty() ? 0.0f : ceilf((float)allMapsVis.size() / cols);
    float curMapRows  = curMapVis.empty()  ? 0.0f : ceilf((float)curMapVis.size()  / cols);
    float sectionGap  = (!allMapsVis.empty() && !curMapVis.empty()) ? 4.0f : 0.0f;
    float panelW = cols * (btnW + 4.0f) + 12.0f;
    float panelH = (allMapsRows + curMapRows) * (btnH + 4.0f)
                 + (allMapsVis.empty() ? 0.0f : headerH + 4.0f)
                 + (curMapVis.empty()  ? 0.0f : headerH + 4.0f)
                 + sectionGap + 12.0f;

    bool flipX     = (ix + sz + panelW > screenW);
    bool edgeTop   = (iy < screenH * 0.25f);
    bool edgeBottom= (!edgeTop && iy + sz > screenH * 0.75f);
    bool vertical  = edgeTop || edgeBottom;

    float px, py;
    if (vertical) {
        px = flipX ? (ix + sz - panelW) : ix;
        px = std::max(0.0f, std::min(px, screenW - panelW));
        py = edgeTop ? iy + sz + 4.0f : iy - panelH - 4.0f;
    } else {
        px = flipX ? ix - panelW - 4.0f : ix + sz + 4.0f;
        py = (iy + panelH > screenH) ? iy + sz - panelH : iy;
    }

    // --- Apply animation transform ---
    float alpha = g_AnimProgress;
    ImVec2 panelPos(px, py);
    ImVec2 panelSize(panelW, panelH);

    if (g_Settings.animStyle == 1) {
        float slideOff = (1.0f - g_AnimProgress) * 20.0f;
        if (vertical)
            panelPos.y += edgeTop ? -slideOff : slideOff;
        else
            panelPos.x += flipX ? slideOff : -slideOff;
    } else if (g_Settings.animStyle == 2) {
        float scale = PopScale(g_AnimProgress);
        float cx = px + panelW * 0.5f;
        float cy = py + panelH * 0.5f;
        panelSize.x *= scale;
        panelSize.y *= scale;
        panelPos.x = cx - panelSize.x * 0.5f;
        panelPos.y = cy - panelSize.y * 0.5f;
    }

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(alpha * 0.95f);

    bool pinned = !g_Settings.closeOnSend;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, pinned ? 1.5f : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Border,
        pinned ? ImVec4(0.86f, 0.75f, 0.31f, alpha * 0.9f) : ImVec4(0, 0, 0, 0));

    ImGuiWindowFlags panelFlags =
        ImGuiWindowFlags_NoTitleBar  | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove      | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoDecoration;

    bool disableItems = (g_AnimProgress < 0.1f);
    if (disableItems) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

    if (ImGui::Begin("##sa_panel", nullptr, panelFlags)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

        // FIX: collect right-click outside PushID scope so popup ID matches BeginPopup
        static int    s_RightClickIdx = -1;
        static int    s_HoverIdx      = -1;
        static double s_HoverStart    = 0.0;
        int pendingRightClick = -1;

        ImVec4 headerColour(0.86f, 0.75f, 0.31f, alpha);

        bool anyHovered = false;
        int sectionCol = 0; // column counter within current section
        for (int i = 0; i < (int)visible.size(); ++i) {
            // Section headers
            if (i == 0 && !allMapsVis.empty()) {
                ImGui::TextColored(headerColour, "All Maps");
                sectionCol = 0;
            } else if (i == allMapsCount && !curMapVis.empty()) {
                if (!allMapsVis.empty()) ImGui::Dummy(ImVec2(0.0f, sectionGap));
                const std::string& mapName = MapData::GetMapName(MapData::NormalizeMapId(g_CurrentMapId));
                ImGui::TextColored(headerColour, "%s", mapName.c_str());
                sectionCol = 0;
            }

            if (sectionCol % cols != 0) ImGui::SameLine();
            ImGui::PushID(i);
            std::string lbl = GW2API::ResolveDisplay(visible[i]->shortLabel);
            if (ImGui::Button(lbl.c_str(), ImVec2(btnW, btnH)))
                PostMessage(*visible[i], "");
            // Badge overlay for multi-line messages
            if (visible[i]->fullText.find('\n') != std::string::npos) {
                int n = (int)std::count(visible[i]->fullText.begin(), visible[i]->fullText.end(), '\n') + 1;
                char badge[8];
                snprintf(badge, sizeof(badge), "\xc3\x97%d", n); // ×N (UTF-8)
                ImDrawList* bdl = ImGui::GetWindowDrawList();
                ImVec2 bMin = ImGui::GetItemRectMin();
                ImVec2 bMax = ImGui::GetItemRectMax();
                ImVec2 ts   = ImGui::CalcTextSize(badge);
                ImVec2 rMax = { bMax.x - 2.0f, bMin.y + ts.y + 4.0f };
                ImVec2 rMin = { rMax.x - ts.x - 6.0f, bMin.y + 2.0f };
                bdl->AddRectFilled(rMin, rMax, IM_COL32(0, 0, 0, 160), 3.0f);
                bdl->AddText({ rMin.x + 3.0f, rMin.y + 2.0f }, IM_COL32(200, 170, 100, 255), badge);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                pendingRightClick = i;
            if (ImGui::IsItemHovered()) {
                anyHovered = true;
                if (s_HoverIdx != i) { s_HoverIdx = i; s_HoverStart = ImGui::GetTime(); }
                if (ImGui::GetTime() - s_HoverStart >= 1.0) {
                    std::string tip = GW2API::ResolveDisplay(visible[i]->fullText);
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
            ImGui::PopID();
            ++sectionCol;
        }
        if (!anyHovered) s_HoverIdx = -1;

        // Open popup after all PushID scopes are closed
        if (pendingRightClick >= 0) {
            s_RightClickIdx = pendingRightClick;
            ImGui::OpenPopup("##chan");
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        if (ImGui::BeginPopup("##chan")) {
            if (s_RightClickIdx >= 0 && s_RightClickIdx < (int)visible.size()) {
                const ChatMessage* m = visible[s_RightClickIdx];
                bool isML = m->fullText.find('\n') != std::string::npos;
                const ImVec2 itemSz(120, 28);
                if (!isML && ImGui::Selectable("Say",   false, 0, itemSz)) { PostMessage(*m, "/say ");   ImGui::CloseCurrentPopup(); }
                if (             ImGui::Selectable("Party", false, 0, itemSz)) { PostMessage(*m, "/party "); ImGui::CloseCurrentPopup(); }
                if (             ImGui::Selectable("Squad", false, 0, itemSz)) { PostMessage(*m, "/squad "); ImGui::CloseCurrentPopup(); }
                if (!isML && ImGui::Selectable("Map",   false, 0, itemSz)) { PostMessage(*m, "/map ");   ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);

        ImGui::PopStyleVar();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (disableItems) ImGui::PopItemFlag();
}
