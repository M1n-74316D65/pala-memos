# Integrating pala-memos into the Pala Note firmware

These are the edits to make in **your own copy** of the Pala Note firmware. This repo
ships the new files and the change instructions only — it does not include any of the
base firmware (© Paul Lagier).

Firmware layout assumed: an Arduino sketch `pala_note/` with `pala_note.ino`,
`config.h`, `types.h`, and modules under `src/app/`. Build with **Arduino ESP32 core
3.x** (same as the stock firmware).

What you get when done: the stock OpenAI-Whisper transcription is replaced by your
self-hosted **Memos** server — notes are AI-transcribed via Memos, published as memos
with their tag, and a two-way task list syncs with checkbox lines inside memos, all
configurable from the device's web portal (no recompiling, no hardcoded Wi-Fi).

---

## 1. Drop in the new files

Copy from this repo into the sketch:

```
memos/memos.h                ->  src/app/memos/memos.h
memos/memos.cpp              ->  src/app/memos/memos.cpp
config_store/config_store.h  ->  src/app/config_store.h
config_store/config_store.cpp -> src/app/config_store.cpp
extras/certs.h               ->  certs.h            (sketch root)
extras/secrets_inc.h         ->  secrets_inc.h      (sketch root)
extras/secrets.example.h     ->  secrets.example.h  (sketch root)
```

Optional (Bluetooth config, step 11):

```
shtc3/shtc3.h                ->  src/app/shtc3.h
shtc3/shtc3.cpp              ->  src/app/shtc3.cpp
ble_service/ble_service.h    ->  src/app/ble_service.h
ble_service/ble_service.cpp  ->  src/app/ble_service.cpp
```

`memos.cpp` expects to sit in `src/app/memos/` — its includes are relative to that
location (`../../../config.h`, `../notes.h`, `../config_store.h`, etc.).
`config_store`, `ble_service` and `shtc3` expect `src/app/` directly.

---

## 2. Secrets

Make your own `secrets.h` at the sketch root (git-ignored) from `secrets.example.h`:

```c
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID            "YOUR_WIFI_SSID"
#define WIFI_PASS            "YOUR_WIFI_PASSWORD"
#define MEMOS_INSTANCE_URL   "https://memos.example.com"
#define MEMOS_ACCESS_TOKEN   "your_pat_token_here"
#define MEMOS_VISIBILITY     "PRIVATE"

#endif
```

Everything in `secrets.h` is only a **first-boot default** — after the first flash
you change Wi-Fi, Memos URL, token, visibility, timezone and portal PIN from the
device's web portal (`/setup`), stored on the SD card at `/notes/config.txt`.
Keep the stock `OPENAI_KEY` line out — it is no longer used.

> The stock firmware also has a `secrets.h` with `WIFI_SSID/WIFI_PASS`. The modules
> below read through `secrets_inc.h`, which prefers your real `secrets.h` and falls
> back to the example so a clean checkout still compiles.

Add `secrets.h` to the sketch's `.gitignore` if it isn't already.

---

## 3. `types.h` — task struct + state

Add the `MemosTask` struct, and add `STATE_TASKS` to the `AppState` enum (e.g. before
`STATE_ERROR`):

```c
// in enum AppState { ... , STATE_TRANSFER,
  STATE_TASKS,
// STATE_ERROR };

struct MemosTask {
  char memoName[64];
  char text[80];
  bool done;
  int  lineIdx;
  bool dirty;
};
```

Bump the menu count:

```c
#define MENU_COUNT  5   // was 4
```

---

## 4. `globals.h` — task list extern

```c
extern std::vector<MemosTask> taskList;
```

---

## 5. `notes.h` / `notes.cpp` — memo name in note meta

The module stores the id of the memo each note was published into. Widen
`writeNoteMeta` to take it (default keeps all existing calls compiling):

```c
// notes.h
void   writeNoteMeta(int num, const char* tag, const char* memoName = nullptr);
```

```cpp
// notes.cpp — replace the whole function
void writeNoteMeta(int num, const char* tag, const char* memoName) {
  String path = noteMetaPath(num);
  String existingCreated = readNoteMetaValue(num, "created_utc");
  String existingMemo = readNoteMetaValue(num, "memo_name");
  String created = existingCreated.length() ? existingCreated : currentUtcIso();
  String memo = memoName ? String(memoName) : existingMemo;

  File f = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!f) return;
  f.print("created_utc="); f.println(created);
  f.print("tag="); f.println(tag ? tag : "");
  f.print("synced=");
  bool hasText = false;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (noteIndex[i].num == num) { hasText = noteIndex[i].hasText; break; }
  f.println(hasText ? "1" : "0");
  if (memo.length() > 0) {
    f.print("memo_name="); f.println(memo);
  }
  f.close();
}
```

---

## 6. `rtc.h` / `rtc.cpp` — timezone from config

The portal-configurable timezone needs a hook in the RTC module.

```c
// rtc.h
void    rtcApplyTimezone();
```

```cpp
// rtc.cpp — add near the top with the other includes
#include "config_store.h"
```

```cpp
// rtc.cpp — add this function
void rtcApplyTimezone() {
  int off = configGetTimezoneOffsetMin();
  if (off == 60 || off == 120) {
    setenv("TZ", TZ_BERLIN, 1);
  } else {
    char tz[24];
    int hours = off / 60;
    int mins = abs(off % 60);
    if (mins == 0) snprintf(tz, sizeof(tz), "UTC%+d", -hours);
    else           snprintf(tz, sizeof(tz), "UTC%+d:%02d", -hours, mins);
    setenv("TZ", tz, 1);
  }
  tzset();
}
```

Then call it at the end of the stock `utcTmToEpoch()` (after its `mktime`, before
`return epoch;`), so conversions back out of UTC use the configured zone.

---

## 7. `network.h` / `network.cpp` — swap the backend, add portal settings

### 7a. Remove the stock OpenAI transcription

The module provides `transcribe()` / `transcribeAll()` now, so delete the stock
(OpenAI) ones to avoid duplicate symbols:

- In `network.h`: remove the declarations
  `bool transcribe(const String& wavPath, int noteNum);` and `void transcribeAll();`
- In `network.cpp`: remove the matching functions and any static helper used only by
  them (the whisper response parser). Search for `api.openai.com` / `OPENAI_KEY` —
  that whole transcription block goes away. Keep everything portal/transfer related.

### 7b. Robust Wi-Fi helpers (config-driven instead of hardcoded)

Append to `network.cpp` (these read Wi-Fi credentials from `config_store` instead of
hardcoded `secrets.h`):

```cpp
static void wifiEventCallback(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[WiFi-Event] Got IP: %s\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WiFi-Event] Disconnected (Reason %d)\n", info.wifi_sta_disconnected.reason);
      break;
    default: break;
  }
}

bool wifiConnect(int maxWaitSeconds, bool showUi) {
  String ssid = configGetWifiSsid();
  String pass = configGetWifiPass();
  ssid.trim();
  pass.trim();

  static bool eventRegistered = false;
  if (!eventRegistered) {
    WiFi.onEvent(wifiEventCallback);
    eventRegistered = true;
  }

  WiFi.persistent(false);
  WiFi.disconnect(false, true);
  delay(50);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  WiFi.setAutoReconnect(true);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

  if (ssid.length() == 0) return false;
  WiFi.begin(ssid.c_str(), pass.c_str());

  int maxTries = maxWaitSeconds * 2;
  int tries = 0;
  if (showUi) showWifiConnecting(0, maxTries);
  while (WiFi.status() != WL_CONNECTED && tries < maxTries) {
    delay(500);
    tries++;
    if (showUi) showWifiConnecting(tries, maxTries);
  }
  return WiFi.status() == WL_CONNECTED;
}

void wifiDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
```

```c
// network.h
bool wifiConnect(int maxWaitSeconds = 15, bool showUi = true);
void wifiDisconnect();
```

(`network.cpp` needs `#include "esp_wifi.h"` for the two `esp_wifi_*` calls, and
`#include "config_store.h"`.)

### 7c. Portal: settings page + optional PIN

Add these handlers to `network.cpp` (self-contained — they reuse the stock
`portalCss()` / `htmlEscape()` helpers):

```cpp
static String s_portalCookie;

static void sendPortalUnlockPage(const char* msg) {
  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala PIN</title>" + portalCss() + "</head><body><div class='wrap'>";
  html += "<h1>pala<br>portal</h1><div class='sub'>enter pin</div>";
  html += "<div class='card' style='margin-top:24px'>";
  if (msg && msg[0]) {
    html += "<div style='margin-bottom:12px;color:#c0392b;font-weight:700'>";
    html += htmlEscape(String(msg));
    html += "</div>";
  }
  html += "<form action='/unlock' method='post'>";
  html += "<div style='margin-bottom:16px'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px'>PIN</label>";
  html += "<input type='password' name='pin' maxlength='8' inputmode='numeric' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box'></div>";
  html += "<button type='submit' class='btn primary' style='cursor:pointer;font-size:14px;padding:12px 24px;border:none'>Unlock</button>";
  html += "</form></div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

static bool portalCookieValid() {
  if (s_portalCookie.length() == 0) return false;
  if (!transferServer.hasHeader("Cookie")) return false;
  return transferServer.header("Cookie").indexOf("pala_sess=" + s_portalCookie) >= 0;
}

static void issuePortalCookie() {
  char tok[17];
  snprintf(tok, sizeof(tok), "%08x%08x", (unsigned)esp_random(), (unsigned)esp_random());
  s_portalCookie = String(tok);
  transferServer.sendHeader("Set-Cookie", "pala_sess=" + s_portalCookie + "; Path=/; HttpOnly");
}

static bool requirePortalAuth() {
  String saved = configGetPortalPin();
  if (saved.length() == 0) return true;
  if (portalCookieValid()) return true;
  sendPortalUnlockPage(nullptr);
  return false;
}

void handlePortalUnlock() {
  if (transferServer.method() != HTTP_POST) {
    sendPortalUnlockPage(nullptr);
    return;
  }
  String saved = configGetPortalPin();
  if (saved.length() == 0 ||
      (transferServer.hasArg("pin") && transferServer.arg("pin") == saved)) {
    issuePortalCookie();
    transferServer.sendHeader("Location", "/");
    transferServer.send(303);
    return;
  }
  sendPortalUnlockPage("Invalid PIN");
}

void handleSetupPage() {
  if (!requirePortalAuth()) return;
  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>Pala Settings</title>" + portalCss() + "</head><body><div class='wrap'>";

  html += "<h1>pala<br>settings</h1>";
  html += "<div class='sub' style='margin-bottom:20px'>device configuration · <a href='/' style=\"color:inherit\">back to portal</a></div>";

  if (transferServer.hasArg("msg") && transferServer.arg("msg") == "saved") {
    html += "<div class='card' style='background:#e8f5e9;border-color:#2e7d32;margin-bottom:16px;font-weight:600;color:#1b5e20;'>Settings saved successfully to device SD storage!</div>";
  }

  html += "<div class='card'>";
  html += "<form action='/setup/save' method='post'>";
  html += "<div style='font-size:16px;font-weight:800;margin-bottom:12px;'>Wi-Fi & Time</div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>WIFI SSID</label>";
  html += "<input type='text' name='ssid' value='" + htmlEscape(configGetWifiSsid()) + "' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;' required></div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>WIFI PASSWORD</label>";
  html += "<input type='password' name='pass' value='" + htmlEscape(configGetWifiPass()) + "' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'></div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>TIMEZONE OFFSET (MINUTES; 60 or 120 = Europe/Berlin with DST)</label>";
  html += "<input type='number' name='tz' value='" + String(configGetTimezoneOffsetMin()) + "' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'></div>";

  html += "<hr style='border:none;border-top:1.5px dashed #ccc;margin:24px 0;'>";
  html += "<div style='font-size:16px;font-weight:800;margin-bottom:6px;'>usememos Sync & AI</div>";
  html += "<div style='font-size:13px;color:#666;margin-bottom:14px;'>Connect your self-hosted or cloud Memos instance for AI transcription and automatic memo publishing.</div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>MEMOS INSTANCE URL (e.g. https://memos.example.com)</label>";
  html += "<input type='text' name='memos_url' value='" + htmlEscape(configGetMemosUrl()) + "' placeholder='https://memos.example.com' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'></div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>MEMOS ACCESS TOKEN (PAT)</label>";
  html += "<input type='password' name='memos_token' value='" + htmlEscape(configGetMemosToken()) + "' placeholder='eyJhbGciOi...' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'></div>";

  html += "<div style='margin-bottom:16px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>MEMO DEFAULT VISIBILITY</label>";
  html += "<select name='memos_vis' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'>";
  String curVis = configGetMemosVisibility();
  html += "<option value='PRIVATE' " + String(curVis == "PRIVATE" ? "selected" : "") + ">Private (Only you)</option>";
  html += "<option value='PROTECTED' " + String(curVis == "PROTECTED" ? "selected" : "") + ">Protected (Signed-in users)</option>";
  html += "<option value='PUBLIC' " + String(curVis == "PUBLIC" ? "selected" : "") + ">Public (Everyone)</option>";
  html += "</select></div>";

  html += "<hr style='border:none;border-top:1.5px dashed #ccc;margin:24px 0;'>";
  html += "<div style='font-size:16px;font-weight:800;margin-bottom:6px;'>Security</div>";
  html += "<div style='margin-bottom:20px;'><label style='display:block;font-size:13px;font-weight:700;margin-bottom:6px;'>PORTAL SECURITY PIN (OPTIONAL 4 DIGITS)</label>";
  html += "<input type='password' maxlength='4' name='pin' value='" + htmlEscape(configGetPortalPin()) + "' placeholder='Leave blank for open access' style='font:inherit;padding:10px 14px;border:1.5px solid #111;border-radius:12px;background:#fff;width:100%;box-sizing:border-box;'></div>";

  html += "<button type='submit' class='btn primary' style='cursor:pointer;font-size:14px;padding:12px 24px;border:none;'>Save Settings</button>";
  html += "</form></div></div></body></html>";
  transferServer.send(200, "text/html", html);
}

void handleSetupSave() {
  if (!requirePortalAuth()) return;
  String ssid       = transferServer.hasArg("ssid")        ? transferServer.arg("ssid") : "";
  String pass       = transferServer.hasArg("pass")        ? transferServer.arg("pass") : "";
  String memosUrl   = transferServer.hasArg("memos_url")   ? transferServer.arg("memos_url")   : "";
  String memosToken = transferServer.hasArg("memos_token") ? transferServer.arg("memos_token") : "";
  String memosVis   = transferServer.hasArg("memos_vis")   ? transferServer.arg("memos_vis")   : "PRIVATE";
  int tz            = transferServer.hasArg("tz")   ? transferServer.arg("tz").toInt() : LOCAL_TIME_OFFSET_MIN;
  String pin        = transferServer.hasArg("pin")  ? transferServer.arg("pin")  : "";

  configSet(ssid, pass, memosUrl, memosToken, memosVis, tz, pin);

  transferServer.sendHeader("Location", "/setup?msg=saved");
  transferServer.send(303);
}
```

Register the routes at the end of the stock `setupTransferServer()`:

```cpp
  // at the top of setupTransferServer():
  const char* hdrs[] = { "Cookie" };
  transferServer.collectHeaders(hdrs, 1);
  // with the other transferServer.on(...) lines:
  transferServer.on("/unlock", HTTP_GET, handlePortalUnlock);
  transferServer.on("/unlock", HTTP_POST, handlePortalUnlock);
  transferServer.on("/setup", HTTP_GET, handleSetupPage);
  transferServer.on("/setup/save", HTTP_POST, handleSetupSave);
```

Recommended (PIN protection): add `if (!requirePortalAuth()) return;` as the first
line of the stock portal handlers (`handlePortalRoot`, `handleExportTxt`,
`sendFileByNum`, `handleTagsPage`, `handleTagAdd`, `handleTagDelete`,
`handleNoteDelete`, `handlePortalJson`). With no PIN set this is a no-op.

Clear the session cookie in `stopTransferMode()`:

```cpp
void stopTransferMode() {
  s_portalCookie = "";
  // ... rest unchanged
}
```

And declare in `network.h`:

```c
void handlePortalUnlock();
void handleSetupPage();
void handleSetupSave();
```

---

## 8. `pala_note.ino` — wire it up

**Includes** (with the other `src/app` includes):

```c
#include "src/app/config_store.h"
#include "src/app/memos/memos.h"
```

Remove `#include "secrets.h"` (credentials now flow through `config_store`).

**Globals** — with the other definitions:

```c
std::vector<MemosTask> taskList;
```

**Menu items** — add `"Tasks"` after `"Notes"`:

```c
const char* MENU_ITEMS[] = { "Notes", "Tasks", "Tags", "Sync", "Settings" };
```

**Menu select handler** — insert the Tasks branch and shift the remaining indices
(stock indices were Notes=0, Tags=1, Sync=2, Settings=3):

```c
} else if (menuCursor == 0) {
  activeFilter = -1; listCursor = 0;
  state = STATE_NOTE_LIST;
  showNoteList(listCursor);
} else if (menuCursor == 1) {          // Tasks
  taskCursor = 0;
  state = STATE_TASKS;
  showTasksScreen(taskCursor);
} else if (menuCursor == 2) {          // Tags
  tagCursor = 0;
  state = STATE_TAG_BROWSER;
  showTagBrowser(tagCursor);
} else if (menuCursor == 3) {          // Sync
  startSyncFlow();
} else {                               // Settings
  settingsCursor = 0;
  state = STATE_SETTINGS;
  showSettings(settingsCursor);
}
```

(declares `int taskCursor = 0;` alongside `menuCursor` etc.)

**Sync flow** — replace the whole stock `startSyncFlow()` with:

```cpp
void startSyncFlow() {
  if (!wifiConnect(15, true)) {
    wifiDisconnect();
    wl_status_t st = WiFi.status();
    if (st == WL_NO_SSID_AVAIL)       showError("NO SSID FOUND");
    else if (st == WL_CONNECT_FAILED) showError("WIFI AUTH FAIL");
    else                              showError("NO WIFI");
    soundBack();
    delay(2000);
    if (wakeToMenuRequested) { state = STATE_MENU; showMenu(menuCursor); }
    else { state = STATE_IDLE; showIdle(); }
    return;
  }

  // Clock sync
  syncTimeFromNTP(6000);

  // Memos API health check
  String memosErr = "";
  if (!testMemosConnection(memosErr)) {
    wifiDisconnect();
    showError(memosErr.c_str());
    soundBack();
    delay(2200);
    if (wakeToMenuRequested) { state = STATE_MENU; showMenu(menuCursor); }
    else { state = STATE_IDLE; showIdle(); }
    return;
  }

  // Two-way task sync
  pushDirtyTasks();
  fetchMemosTasks();

  // Transcription & memo publishing
  int successCount = 0, failedCount = 0;
  int totalPending = transcribeAll(successCount, failedCount);
  loadIndex();
  wifiDisconnect();

  if (totalPending == 0) {
    showDone();
    soundSuccess();
    delay(1400);
  } else if (failedCount == 0) {
    showDone();
    soundSuccess();
    delay(1600);
  } else if (successCount > 0) {
    showError("SYNC PARTIAL");
    soundBack();
    delay(2200);
  } else {
    showError("SYNC FAILED");
    soundBack();
    delay(2200);
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else {
    state = STATE_IDLE;
    showIdle();
  }
}
```

**Tasks screen handler** — add a branch in the main loop's state `if/else` chain:

```cpp
else if (state == STATE_TASKS) {
  ButtonEvent rec = readButtonEvent(BTN_REC);
  ButtonEvent pwr = readButtonEvent(BTN_PWR);

  if (pwr == EV_SINGLE) {
    soundNext();
    if (taskList.size() > 0) taskCursor = (taskCursor + 1) % taskList.size();
    showTasksScreen(taskCursor);
  } else if (rec == EV_SINGLE) {
    soundSelect();
    if (taskCursor >= 0 && taskCursor < (int)taskList.size()) {
      taskList[taskCursor].done = !taskList[taskCursor].done;
      taskList[taskCursor].dirty = true;
      saveTasksToSD();
      showTasksScreen(taskCursor);
    }
  } else if (isBrowseBack(rec, pwr)) {
    soundBack();
    state = STATE_MENU;
    showMenu(menuCursor);
  }
}
```

**Setup** — after the SD/index loading, add:

```c
loadTasksFromSD();
configInit();
```

(`configInit()` loads `/notes/config.txt` and applies the configured timezone.)

**Optional: hourly auto-sync** — if your firmware's deep-sleep wakeup already
distinguishes timer wakeups, this block re-syncs unattended while sleeping. Put it
in `setup()` after `configInit()`, before the normal wake handling. It re-enters
deep sleep for another hour either way:

```cpp
if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
  int unSynced = 0;
  for (const auto& entry : noteIndex) {
    if (!entry.hasText) unSynced++;
  }
  if (unSynced > 0 && configGetWifiSsid().length() > 0
      && configGetWifiSsid() != "YOUR_WIFI_SSID") {
    startSyncFlow();
  }
  enterUltraSleep(3600); // adjust to your sleep API: sleep again for 1 hour
}
```

(This expects your `enterUltraSleep` to accept an optional seconds argument for a
timer wakeup — add `uint32_t sleepSeconds` handling there if your copy doesn't
have it: when > 0, call `esp_sleep_enable_timer_wakeup((uint64_t)sleepSeconds * 1000000ULL)` before sleeping.)

---

## 9. `ui.h` / `ui.cpp` — tasks screen

```c
// ui.h
void showTasksScreen(int cursor);
```

```cpp
// ui.cpp
void showTasksScreen(int cursor) {
  clearWhite();
  int total = (int)taskList.size();
  int doneCount = 0;
  for (const auto& t : taskList) if (t.done) doneCount++;

  char rightInfo[24];
  if (total > 0) snprintf(rightInfo, sizeof(rightInfo), "%d/%d done", doneCount, total);
  else           snprintf(rightInfo, sizeof(rightInfo), "0 tasks");

  drawHeader("tasks", rightInfo);

  if (total <= 0) {
    drawMinimalDocIcon(100, 75, BLACK);
    drawStrC(100, 110, "no tasks yet", 1, BLACK);
    drawStrC(100, 130, "sync with memos", 1, BLACK);
    refresh();
    return;
  }

  const int pageSize = 3;
  int pageStart = (cursor / pageSize) * pageSize;
  const int y0 = 38, cardH = 38, gapY = 6;
  int shown = min(pageSize, total - pageStart);

  for (int i = 0; i < shown; i++) {
    int idx = pageStart + i;
    int y = y0 + i * (cardH + gapY);
    bool active = (idx == cursor);

    if (active) fillRoundRect(14, y, 172, cardH, 7, BLACK);
    else        strokeRoundRect(14, y, 172, cardH, 7, 1, BLACK);

    uint8_t fg = active ? WHITE : BLACK;
    uint8_t boxBg = active ? BLACK : WHITE;

    int boxX = 22, boxY = y + 11, boxSize = 16;
    if (taskList[idx].done) {
      fillRoundRect(boxX, boxY, boxSize, boxSize, 3, fg);
      drawCheckSmall(boxX + boxSize/2, boxY + boxSize/2, boxBg);
    } else {
      strokeRoundRect(boxX, boxY, boxSize, boxSize, 3, 1, fg);
    }

    int textX = 46, textY = y + 12;
    drawStrFit(textX, textY, 134, taskList[idx].text, 1, fg);

    if (taskList[idx].done) {
      int tw = min(textW(taskList[idx].text, 1), 134);
      hline(textX, textY + 6, tw, fg);
    }
  }

  int totalPages = (total + pageSize - 1) / pageSize;
  int curPage = cursor / pageSize;
  if (totalPages > 1) drawPageDots(curPage, totalPages);
  else                drawTinyHint("rec do", "pwr next");

  refresh();
}
```

**Optional:** the stock `showMenu()` lays out 4 tiles in one column; a 5th tile may
clip at the bottom of the 200 px screen. Tighten the vertical spacing (the exact
constants vary by firmware version — look for the `y0`/`step`/tile height in
`showMenu` and squeeze ~10%).

---

## 10. Build settings

The sketch grows past the default 1.25 MB app partition (network code + TLS). In
the Arduino IDE choose a larger app partition, e.g.
**Tools → Partition Scheme → "Minimal SPIFFS (1.9MB APP)"** or **"Huge APP (3MB No
OTA)"**. Safe: the project uses no OTA and no internal flash filesystem (all data
lives on the SD card).

Symptom if you skip this: *"Sketch too big / text section exceeds available space."*

---

## 11. Optional: Bluetooth config & status (ble_service)

Adds a BLE GATT service: phone-side apps (e.g. Web Bluetooth) can read device
status as JSON and write all settings (Wi-Fi, Memos URL/token/visibility, timezone,
portal PIN) without opening the portal.

Drop in `shtc3/` and `ble_service/` (step 1), then:

**`types.h`:** add `STATE_BLE` to `AppState` and bump `#define SETTINGS_COUNT 4` (was 3).

**`ui.h`/`ui.cpp`** — settings screen shows the new entry (edit `showSettings` to
draw a 4th row labelled "bluetooth"), plus:

```c
// ui.h
void showBleMode();
```

```cpp
// ui.cpp — needs #include "ble_service.h"
void showBleMode() {
  clearWhite();
  drawKicker("bluetooth", 16);
  fillRoundRect(26, 48, 148, 58, 16, BLACK);
  drawStrInBox(26, 48, 148, 24, "pala ble", 1, WHITE);
  drawStrInBox(26, 74, 148, 24, bleIsConnected() ? "connected" : "ready", 1, WHITE);
  drawStrC(100, 124, "open web bluetooth", 1, BLACK);
  drawStrC(100, 146, "pala-note", 1, BLACK);
  drawStrC(100, 169, "pwr or double back", 1, BLACK);
  refresh();
}
```

**`pala_note.ino`:**

```c
#include "src/app/ble_service.h"
```

```c
const char* SETTINGS_ITEMS[] = { "Sounds", "Transfer", "Bluetooth", "Device" };
```

Settings select handler — map index 2 to the BLE screen (shift "Device" to 3):

```c
} else if (settingsCursor == 2) {
  state = STATE_BLE;
  bleServiceStart();
  showBleMode();
}
```

Main loop — add the branch:

```cpp
else if (state == STATE_BLE) {
  ButtonEvent rec = readButtonEvent(BTN_REC);
  ButtonEvent pwr = readButtonEvent(BTN_PWR);
  if (pwr == EV_SINGLE || isBrowseBack(rec, pwr)) {
    soundBack();
    bleServiceStop();
    settingsCursor = 0;
    state = STATE_SETTINGS;
    showSettings(settingsCursor);
  }
}
```

BLE config wire format (one write, newline-separated):
`[PIN\n] ssid\npass\nmemos_url\nmemos_token\nmemos_vis\ntz_offset_min\nnew_pin`
(lines 2+ of trailing fields are optional; the PIN line is required only when a
portal PIN is already set.)

---

## 12. First start

1. Flash, open the serial monitor once and confirm `Memos URL` / stored Wi-Fi lines.
2. On the device: Settings → Transfer (web portal), browse to `/setup`, fill in
   your Memos URL + access token, save.
3. Menu → Sync. Health check passes, tasks download, pending notes transcribe and
   publish to Memos.

Server preparation (token, AI transcription, HTTPS notes): see
[`docs/memos_server_setup.md`](docs/memos_server_setup.md). What lands where and
how conflicts resolve: [`docs/sync_model.md`](docs/sync_model.md).

---

## APIs used from the base firmware

The modules rely on these existing symbols (verify the names match your firmware
version):

- Notes/index: `noteIndex`, `updateIndexHasText`, `writeNoteMeta`, `NOTES_DIR`
- UI: `showTranscribing`, `showWifiConnecting`, `showDone`, `showError`, `showIdle`,
  `showMenu`, `showSettings`, `drawHeader`, `drawCheckSmall`, `drawPageDots`,
  `drawMinimalDocIcon`, `drawTinyHint`, `drawStrFit`, `drawStrC`, `drawStrInBox`,
  `drawKicker`, `textW`, `hline`, `fillRoundRect`, `strokeRoundRect`, `clearWhite`,
  `refresh`, `BLACK`, `WHITE`
- Buttons: `readButtonEvent`, `isBrowseBack`, `BTN_REC`, `BTN_PWR`,
  `EV_SINGLE` (button engine as in current firmware builds)
- Time: `syncTimeFromNTP`, `currentUtcIso`, `TZ_BERLIN`, `LOCAL_TIME_OFFSET_MIN`
- Sounds: `soundNext`, `soundSelect`, `soundBack`, `soundSuccess`
- Globals: `state`, `menuCursor`, `listCursor`, `tagCursor`, `settingsCursor`,
  `activeFilter`, `wakeToMenuRequested`, `transferServer`
- Portal helpers: `portalCss`, `htmlEscape`
