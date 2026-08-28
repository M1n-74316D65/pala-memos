#pragma once
// ─── usememos integration (community add-on) ────────────────────────────────
// One-way boundary: firmware code calls this module; this module never
// includes network.h. Wiring instructions for stock firmware: INTEGRATION.md.
#include <Arduino.h>
#include "WiFiClientSecure.h"

// TLS shared helper (used by the Memos API and other HTTPS services, e.g. weather)
bool palaSecureConnect(WiFiClientSecure& client, const char* host, uint16_t port, uint32_t timeoutMs);

// Health
bool testMemosConnection(String& outError);

// Notes: AI transcription + memo publishing
bool transcribe(const String& wavPath, int noteNum);
bool transcribeWithMemos(const String& wavPath, int noteNum, String& outText);
bool createMemo(const String& content, const String& visibility, String& outMemoName);
int  transcribeAll(int& outSuccess, int& outFailed);
void transcribeAll();

// Tasks: two-way sync (Memos checkbox list <-> MemosTask)
bool fetchMemosTasks();
bool pushDirtyTasks();
void loadTasksFromSD();
void saveTasksToSD();
