#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include "nexus/Nexus.h"
#include "mumble/Mumble.h"

extern AddonAPI_t*  APIDefs;
extern uint32_t     g_CurrentMapId;
extern HWND         g_GameHandle;
extern Mumble::Data* g_MumbleLink;

// List of filenames (no path) found in the icons/ subdirectory
extern std::vector<std::string> g_CustomIconNames;

struct Settings {
    bool        directPost     = true;
    bool        closeOnSend    = false;
    float       iconSize       = 48.0f;
    float       iconX          = -9999.0f;
    float       iconY          = -9999.0f;
    bool        iconLocked     = false;
    int         columns        = 2;
    int         animStyle      = 0; // 0=Fade, 1=Slide, 2=Pop
    std::string customIconName;     // "" = embedded default
    int         multiLineDelay = 500; // ms between sequential line sends
    std::string messagePrefix;        // prepended to each sent line (after channel command)
    int         channel        = 0;   // sticky chat channel, index into Channels::kAll (0 = Say)
    bool        usePieTheme    = true; // tint UI to match Pie UI's broadcast theme when available
};
extern Settings g_Settings;

// Returns the Nexus texture ID for a given icon filename ("" = default).
inline std::string GetIconTexId(const std::string& filename) {
    return filename.empty() ? "TEX_SA_ICON" : ("TEX_SA_ICON_" + filename);
}

void SaveSettings();
void ScanIconDir();
