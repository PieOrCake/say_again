#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct ChatMessage {
    std::string shortLabel;
    std::string fullText;
    std::vector<uint32_t> mapIds; // empty = All Maps
};

extern std::vector<ChatMessage> g_Messages;

// Load/save from addon data directory
void LoadMessages();
void SaveMessages();

// Message manipulation
void MoveMessage(int fromIdx, int toIdx);

// Import/export via Windows file dialog
void ImportMessages();
void ExportMessages();

// One-time import from Chat Shorts addon
bool ChatShortsAvailable();
void ImportFromChatShorts();

// Call each frame to handle async import decision modal
void TickImportModal();
void RenderImportModal();
