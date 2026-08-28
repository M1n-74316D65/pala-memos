// ─── usememos integration (community add-on) ────────────────────────────────
// Pala Note <-> Memos: AI transcription, memo publishing, two-way task sync.
// Install: drop into src/app/memos/ and follow INTEGRATION.md.
#include "Arduino.h"
#include "../../../config.h"
#include "../../../globals.h"
#include "../../../types.h"
#include "memos.h"
#include "../notes.h"
#include "../ui.h"
#include "../config_store.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "SD_MMC.h"
#include "mbedtls/base64.h"
#include "../../../certs.h"

// ─── TLS helpers ─────────────────────────────────────────────────────────────

static const char* palaCaCert() {
  static String pem;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    File f = SD_MMC.open("/notes/ca.pem");
    if (f) {
      pem = f.readString();
      f.close();
      pem.trim();
    }
    if (pem.indexOf("BEGIN CERTIFICATE") < 0) {
      pem = ISRG_ROOT_X1;
    } else {
      Serial.println("[TLS] using /notes/ca.pem");
    }
  }
  return pem.c_str();
}

bool palaSecureConnect(WiFiClientSecure& client, const char* host, uint16_t port, uint32_t timeoutMs) {
  uint32_t sec = (timeoutMs + 999) / 1000;
  if (sec < 6) sec = 6;
  client.setTimeout(sec);
  client.setHandshakeTimeout(sec);
  client.setCACert(palaCaCert());
  bool ok = client.connect(host, port);
  if (!ok) {
    Serial.printf("[TLS] verify/connect failed: %s:%u\n", host, (unsigned)port);
  }
  return ok;
}

// ─── URL / JSON helpers ──────────────────────────────────────────────────────

struct MemosUrlParsed {
  bool isHttps;
  String host;
  int port;
  String basePath;
};

static MemosUrlParsed parseMemosUrl(const String& rawUrl) {
  MemosUrlParsed p;
  p.isHttps = true;
  p.port = 443;
  p.basePath = "";

  String u = rawUrl;
  u.trim();
  if (u.startsWith("http://")) {
    p.isHttps = false;
    p.port = 80;
    u = u.substring(7);
  } else if (u.startsWith("https://")) {
    p.isHttps = true;
    p.port = 443;
    u = u.substring(8);
  }

  int slashIdx = u.indexOf('/');
  if (slashIdx >= 0) {
    p.basePath = u.substring(slashIdx);
    u = u.substring(0, slashIdx);
  }
  while (p.basePath.endsWith("/")) {
    p.basePath = p.basePath.substring(0, p.basePath.length() - 1);
  }

  int colonIdx = u.indexOf(':');
  if (colonIdx >= 0) {
    p.host = u.substring(0, colonIdx);
    p.port = u.substring(colonIdx + 1).toInt();
  } else {
    p.host = u;
  }
  return p;
}

static String jsonEscape(const String& s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

static String parseWhisperText(const String& resp) {
  int keyIdx = resp.indexOf("\"text\"");
  if (keyIdx < 0) return "";
  int colonIdx = resp.indexOf(':', keyIdx);
  if (colonIdx < 0) return "";
  int quoteStart = resp.indexOf('"', colonIdx);
  if (quoteStart < 0) return "";
  quoteStart++; // position immediately after opening quote

  int quoteEnd = quoteStart;
  while (quoteEnd < (int)resp.length()) {
    if (resp[quoteEnd] == '\\' && quoteEnd + 1 < (int)resp.length()) {
      quoteEnd += 2;
      continue;
    }
    if (resp[quoteEnd] == '"') break;
    quoteEnd++;
  }
  if (quoteEnd > (int)resp.length()) return "";

  String text = "";
  for (int i = quoteStart; i < quoteEnd; i++) {
    if (resp[i] == '\\' && i + 1 < quoteEnd) {
      char nx = resp[++i];
      if      (nx == '"')  text += '"';
      else if (nx == '\\') text += '\\';
      else if (nx == 'n')  text += ' ';
      else if (nx == 'r')  continue;
      else if (nx == 't')  text += ' ';
      else                 text += nx;
    } else {
      text += resp[i];
    }
  }
  text.trim();
  return text;
}

// ─── Transcription & publishing ─────────────────────────────────────────────

bool transcribe(const String& wavPath, int noteNum) {
  String outText = "";
  for (int attempt = 0; attempt < 3; attempt++) {
    if (transcribeWithMemos(wavPath, noteNum, outText)) return true;
    if (attempt < 2) { Serial.printf("[Memos] retry %d/2\n", attempt + 1); delay(3000); }
  }
  return false;
}

bool transcribeWithMemos(const String& wavPath, int noteNum, String& outText) {
  outText = "";
  String memosUrl = configGetMemosUrl();
  String token = configGetMemosToken();
  if (memosUrl.length() == 0 || token.length() == 0) return false;

  MemosUrlParsed p = parseMemosUrl(memosUrl);

  File f = SD_MMC.open(wavPath.c_str());
  if (!f) return false;
  size_t fileSize = f.size();

  size_t b64Len = 4 * ((fileSize + 2) / 3);
  String pre = "{\"audio\":{\"content\":\"";
  String post = "\",\"filename\":\"note.wav\",\"contentType\":\"audio/wav\"}}";
  size_t totalLen = pre.length() + b64Len + post.length();

  WiFiClient* client = nullptr;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (p.isHttps) {
    if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 90000)) { f.close(); return false; }
    client = &secureClient;
  } else {
    plainClient.setTimeout(90);
    if (!plainClient.connect(p.host.c_str(), p.port)) { f.close(); return false; }
    client = &plainClient;
  }

  client->printf("POST %s/api/v1/ai:transcribe HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 p.basePath.c_str(), p.host.c_str(), token.c_str(), (unsigned)totalLen);
  client->print(pre);

  uint8_t rawBuf[1536];
  uint8_t b64Buf[2048 + 8];
  int remainderLen = 0;

  while (f.available() || remainderLen > 0) {
    size_t toRead = sizeof(rawBuf) - remainderLen;
    size_t bytesRead = 0;

    if (f.available()) {
      bytesRead = f.read(rawBuf + remainderLen, toRead);
    }
    int totalBytes = remainderLen + bytesRead;

    int bytesToEncode = totalBytes;
    if (f.available()) {
      bytesToEncode = totalBytes - (totalBytes % 3);
    }

    if (bytesToEncode > 0) {
      size_t outLen = 0;
      mbedtls_base64_encode(b64Buf, sizeof(b64Buf), &outLen, rawBuf, bytesToEncode);
      client->write(b64Buf, outLen);
    }

    remainderLen = totalBytes - bytesToEncode;
    if (remainderLen > 0) {
      memmove(rawBuf, rawBuf + bytesToEncode, remainderLen);
    }
  }
  f.close();
  client->print(post);

  uint32_t deadline = millis() + 90000;
  while (!client->available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client->available() || (client->connected() && millis() < deadline)) {
    if (!client->available()) { delay(10); continue; }
    String line = client->readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        Serial.printf("[MemosTranscribe] %s\n", line.c_str());
        client->stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 8192) break;
    }
  }
  client->stop();

  outText = parseWhisperText(resp);
  Serial.printf("[Memos] Response: %s\n", resp.c_str());
  Serial.printf("[Memos] Transcript: \"%s\"\n", outText.c_str());

  if (outText.length() == 0) {
    outText = "(No speech detected)";
  }

  String tp = wavPath; tp.replace(".wav", ".txt");
  File tf = SD_MMC.open(tp.c_str(), FILE_WRITE);
  if (tf) { tf.print(outText); tf.close(); }

  updateIndexHasText(noteNum);
  return true;
}

bool createMemo(const String& content, const String& visibility, String& outMemoName) {
  outMemoName = "";
  String memosUrl = configGetMemosUrl();
  String token = configGetMemosToken();
  if (memosUrl.length() == 0 || token.length() == 0) return false;

  MemosUrlParsed p = parseMemosUrl(memosUrl);

  String vis = visibility.length() ? visibility : "PRIVATE";
  String payload = "{\"content\":\"" + jsonEscape(content) + "\",\"visibility\":\"" + vis + "\"}";

  WiFiClient* client = nullptr;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (p.isHttps) {
    if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 30000)) return false;
    client = &secureClient;
  } else {
    plainClient.setTimeout(30);
    if (!plainClient.connect(p.host.c_str(), p.port)) return false;
    client = &plainClient;
  }

  client->printf("POST %s/api/v1/memos HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Content-Type: application/json\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 p.basePath.c_str(), p.host.c_str(), token.c_str(), (unsigned)payload.length());
  client->print(payload);

  uint32_t deadline = millis() + 30000;
  while (!client->available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client->available() || (client->connected() && millis() < deadline)) {
    if (!client->available()) { delay(10); continue; }
    String line = client->readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0 && line.indexOf(" 201 ") < 0) {
        Serial.printf("[MemosCreate] %s\n", line.c_str());
        client->stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 4096) break;
    }
  }
  client->stop();

  int s = resp.indexOf("\"name\"");
  if (s >= 0) {
    int colon = resp.indexOf(':', s);
    if (colon >= 0) {
      int q1 = resp.indexOf('"', colon);
      if (q1 >= 0) {
        int q2 = resp.indexOf('"', q1 + 1);
        if (q2 > q1) outMemoName = resp.substring(q1 + 1, q2);
      }
    }
  }
  Serial.printf("[Memos] Created Memo: %s\n", outMemoName.c_str());
  return true;
}

bool testMemosConnection(String& outError) {
  outError = "";
  String memosUrl = configGetMemosUrl();
  String token = configGetMemosToken();
  if (memosUrl.length() == 0) {
    outError = "NO MEMOS URL";
    Serial.println("[Memos] Error: No Memos instance URL configured!");
    return false;
  }
  if (token.length() == 0) {
    outError = "NO MEMOS TOKEN";
    Serial.println("[Memos] Error: No Memos access token configured!");
    return false;
  }

  MemosUrlParsed p = parseMemosUrl(memosUrl);

  WiFiClient* client = nullptr;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  Serial.printf("[Memos] Checking API health: %s://%s:%d%s/api/v1/memos?pageSize=1\n",
                p.isHttps ? "https" : "http", p.host.c_str(), p.port, p.basePath.c_str());

  if (p.isHttps) {
    if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 12000)) {
      outError = "MEMOS UNREACHABLE";
      Serial.println("[Memos] Health Check Failed: Could not connect to host.");
      return false;
    }
    client = &secureClient;
  } else {
    plainClient.setTimeout(12);
    if (!plainClient.connect(p.host.c_str(), p.port)) {
      outError = "MEMOS UNREACHABLE";
      Serial.println("[Memos] Health Check Failed: Could not connect to host.");
      return false;
    }
    client = &plainClient;
  }

  client->printf("GET %s/api/v1/memos?pageSize=1 HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n\r\n",
                 p.basePath.c_str(), p.host.c_str(), token.c_str());

  uint32_t deadline = millis() + 12000;
  while (!client->available() && millis() < deadline) delay(20);

  String statusLine = client->readStringUntil('\n');
  statusLine.trim();
  client->stop();

  Serial.printf("[Memos] Health response: %s\n", statusLine.c_str());

  if (statusLine.indexOf(" 200 ") >= 0) {
    Serial.println("[Memos] Health Check Passed (HTTP 200 OK)");
    return true;
  } else if (statusLine.indexOf(" 401 ") >= 0) {
    outError = "MEMOS 401 AUTH";
    Serial.println("[Memos] Health Check Failed: 401 Unauthorized (Check token).");
    return false;
  } else if (statusLine.indexOf(" 404 ") >= 0) {
    outError = "MEMOS 404 URL";
    Serial.println("[Memos] Health Check Failed: 404 Not Found (Check URL).");
    return false;
  } else {
    outError = "MEMOS ERR";
    Serial.printf("[Memos] Health Check Failed: %s\n", statusLine.c_str());
    return false;
  }
}

int transcribeAll(int& outSuccess, int& outFailed) {
  outSuccess = 0;
  outFailed = 0;
  int pending = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (!noteIndex[i].hasText) pending++;
  }
  if (pending == 0) return 0;

  int processed = 0;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (noteIndex[i].hasText) continue;
    showTranscribing(processed, pending);
    char wp[64];
    snprintf(wp, sizeof(wp), "%s/note_%03d.wav", NOTES_DIR, noteIndex[i].num);

    String transcript = "";
    if (transcribeWithMemos(String(wp), noteIndex[i].num, transcript)) {
      outSuccess++;
      processed++;
      if (transcript.length() > 0) {
        String tag = String(noteIndex[i].tag);
        String memoContent = (tag.length() && tag != "Note" && tag != "Untagged") ? ("#" + tag + "\n\n" + transcript) : transcript;
        String memoName = "";
        createMemo(memoContent, configGetMemosVisibility(), memoName);
        if (memoName.length() > 0) {
          writeNoteMeta(noteIndex[i].num, tag.c_str(), memoName.c_str());
        }
      }
    } else {
      outFailed++;
      processed++;
      Serial.printf("[Sync] Note #%03d transcription failed.\n", noteIndex[i].num);
    }
  }
  return pending;
}

void transcribeAll() {
  int succ = 0, fail = 0;
  transcribeAll(succ, fail);
}

// ─── Tasks two-way sync ─────────────────────────────────────────────────────

void saveTasksToSD() {
  File f = SD_MMC.open("/notes/tasks.txt", FILE_WRITE);
  if (!f) return;
  for (const auto& t : taskList) {
    f.printf("%s\t%d\t%d\t%d\t%s\n", t.memoName, t.lineIdx, t.done ? 1 : 0, t.dirty ? 1 : 0, t.text);
  }
  f.close();
}

void loadTasksFromSD() {
  taskList.clear();
  if (!SD_MMC.exists("/notes/tasks.txt")) return;
  File f = SD_MMC.open("/notes/tasks.txt");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    int tab1 = line.indexOf('\t');
    if (tab1 < 0) continue;
    int tab2 = line.indexOf('\t', tab1 + 1);
    if (tab2 < 0) continue;
    int tab3 = line.indexOf('\t', tab2 + 1);
    if (tab3 < 0) continue;
    int tab4 = line.indexOf('\t', tab3 + 1);
    if (tab4 < 0) continue;

    MemosTask t;
    memset(&t, 0, sizeof(t));
    strncpy(t.memoName, line.substring(0, tab1).c_str(), sizeof(t.memoName) - 1);
    t.lineIdx = line.substring(tab1 + 1, tab2).toInt();
    t.done    = (line.substring(tab2 + 1, tab3).toInt() != 0);
    t.dirty   = (line.substring(tab3 + 1, tab4).toInt() != 0);
    strncpy(t.text, line.substring(tab4 + 1).c_str(), sizeof(t.text) - 1);
    taskList.push_back(t);
  }
  f.close();
  Serial.printf("[Tasks] Loaded %d tasks from SD\n", (int)taskList.size());
}

bool fetchMemosTasks() {
  String memosUrl = configGetMemosUrl();
  String token = configGetMemosToken();
  if (memosUrl.length() == 0 || token.length() == 0) return false;

  MemosUrlParsed p = parseMemosUrl(memosUrl);

  WiFiClient* client = nullptr;
  WiFiClientSecure secureClient;
  WiFiClient plainClient;

  if (p.isHttps) {
    if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 15000)) return false;
    client = &secureClient;
  } else {
    plainClient.setTimeout(15);
    if (!plainClient.connect(p.host.c_str(), p.port)) return false;
    client = &plainClient;
  }

  client->printf("GET %s/api/v1/memos?pageSize=30 HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Authorization: Bearer %s\r\n"
                 "Accept: application/json\r\n"
                 "Connection: close\r\n\r\n",
                 p.basePath.c_str(), p.host.c_str(), token.c_str());

  uint32_t deadline = millis() + 15000;
  while (!client->available() && millis() < deadline) delay(20);

  String resp = "";
  bool inBody = false;
  while (client->available() || (client->connected() && millis() < deadline)) {
    if (!client->available()) { delay(10); continue; }
    String line = client->readStringUntil('\n');
    if (!inBody) {
      if (line == "\r" || line == "") inBody = true;
      if (line.startsWith("HTTP/") && line.indexOf(" 200 ") < 0) {
        client->stop(); return false;
      }
    } else {
      resp += line;
      if (resp.length() > 65536) break;
    }
  }
  client->stop();

  std::vector<MemosTask> newTasks;
  int pos = 0;
  while ((pos = resp.indexOf("\"name\":\"", pos)) >= 0) {
    pos += 8;
    int nameEnd = resp.indexOf('"', pos);
    if (nameEnd < 0) break;
    String memoName = resp.substring(pos, nameEnd);

    int contentIdx = resp.indexOf("\"content\":\"", nameEnd);
    if (contentIdx < 0) break;
    contentIdx += 11;
    int contentEnd = contentIdx;
    while (contentEnd < (int)resp.length()) {
      if (resp[contentEnd] == '\\' && contentEnd + 1 < (int)resp.length()) {
        contentEnd += 2;
        continue;
      }
      if (resp[contentEnd] == '"') break;
      contentEnd++;
    }
    String content = resp.substring(contentIdx, contentEnd);
    content.replace("\\n", "\n");
    content.replace("\\\"", "\"");

    int lineStart = 0;
    int lineIndex = 0;
    while (lineStart < (int)content.length()) {
      int lineEnd = content.indexOf('\n', lineStart);
      if (lineEnd < 0) lineEnd = content.length();
      String curLine = content.substring(lineStart, lineEnd);
      curLine.trim();

      bool isDone = false;
      bool isTask = false;
      String taskText = "";

      if (curLine.startsWith("- [ ] ") || curLine.startsWith("- [ ]")) {
        isTask = true; isDone = false;
        taskText = curLine.substring(curLine.indexOf(']') + 1);
      } else if (curLine.startsWith("- [x] ") || curLine.startsWith("- [X] ") ||
                 curLine.startsWith("- [x]") || curLine.startsWith("- [X]")) {
        isTask = true; isDone = true;
        taskText = curLine.substring(curLine.indexOf(']') + 1);
      } else if (curLine.startsWith("[ ] ")) {
        isTask = true; isDone = false;
        taskText = curLine.substring(4);
      } else if (curLine.startsWith("[x] ") || curLine.startsWith("[X] ")) {
        isTask = true; isDone = true;
        taskText = curLine.substring(4);
      }

      if (isTask) {
        taskText.trim();
        taskText.replace("\t", " ");
        taskText.replace("\n", " ");
        taskText.replace("\r", "");

        if (taskText.length() > 0) {
          MemosTask t;
          memset(&t, 0, sizeof(t));
          strncpy(t.memoName, memoName.c_str(), sizeof(t.memoName) - 1);
          strncpy(t.text, taskText.c_str(), sizeof(t.text) - 1);
          t.lineIdx = lineIndex;
          t.done = isDone;
          t.dirty = false;

          for (const auto& existing : taskList) {
            if (strcmp(existing.memoName, t.memoName) == 0 && existing.lineIdx == t.lineIdx && existing.dirty) {
              t.done = existing.done;
              t.dirty = true;
              break;
            }
          }
          newTasks.push_back(t);
        }
      }

      lineStart = lineEnd + 1;
      lineIndex++;
    }

    pos = contentEnd;
  }

  if (newTasks.size() > 0 || taskList.empty()) {
    taskList = newTasks;
    saveTasksToSD();
  }
  Serial.printf("[Tasks] Fetched %d tasks from Memos\n", (int)taskList.size());
  return true;
}

bool pushDirtyTasks() {
  String memosUrl = configGetMemosUrl();
  String token = configGetMemosToken();
  if (memosUrl.length() == 0 || token.length() == 0) return false;

  MemosUrlParsed p = parseMemosUrl(memosUrl);

  for (auto& t : taskList) {
    if (!t.dirty) continue;

    WiFiClient* client = nullptr;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;

    if (p.isHttps) {
      if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 12000)) continue;
      client = &secureClient;
    } else {
      plainClient.setTimeout(12);
      if (!plainClient.connect(p.host.c_str(), p.port)) continue;
      client = &plainClient;
    }

    client->printf("GET %s/api/v1/%s HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Authorization: Bearer %s\r\n"
                   "Accept: application/json\r\n"
                   "Connection: close\r\n\r\n",
                   p.basePath.c_str(), t.memoName, p.host.c_str(), token.c_str());

    uint32_t deadline = millis() + 10000;
    while (!client->available() && millis() < deadline) delay(20);

    String resp = "";
    bool inBody = false;
    while (client->available() || (client->connected() && millis() < deadline)) {
      if (!client->available()) { delay(10); continue; }
      String line = client->readStringUntil('\n');
      if (!inBody) {
        if (line == "\r" || line == "") inBody = true;
      } else {
        resp += line;
        if (resp.length() > 8192) break;
      }
    }
    client->stop();

    int contentIdx = resp.indexOf("\"content\":");
    if (contentIdx < 0) continue;
    contentIdx = resp.indexOf('"', contentIdx + 10);
    if (contentIdx < 0) continue;
    contentIdx++;
    int contentEnd = contentIdx;
    while (contentEnd < (int)resp.length()) {
      if (resp[contentEnd] == '\\' && contentEnd + 1 < (int)resp.length()) {
        contentEnd += 2;
        continue;
      }
      if (resp[contentEnd] == '"') break;
      contentEnd++;
    }
    String content = resp.substring(contentIdx, contentEnd);
    content.replace("\\n", "\n");
    content.replace("\\\"", "\"");

    int lineStart = 0;
    int lineIndex = 0;
    String newContent = "";
    while (lineStart < (int)content.length()) {
      int lineEnd = content.indexOf('\n', lineStart);
      if (lineEnd < 0) lineEnd = content.length();
      String curLine = content.substring(lineStart, lineEnd);

      if (lineIndex == t.lineIdx) {
        if (t.done) {
          curLine.replace("- [ ]", "- [x]");
          curLine.replace("[ ]", "[x]");
        } else {
          curLine.replace("- [x]", "- [ ]");
          curLine.replace("- [X]", "- [ ]");
          curLine.replace("[x]", "[ ]");
          curLine.replace("[X]", "[ ]");
        }
      }

      if (newContent.length()) newContent += "\n";
      newContent += curLine;

      lineStart = lineEnd + 1;
      lineIndex++;
    }

    String patchPayload = "{\"content\":\"" + jsonEscape(newContent) + "\"}";

    if (p.isHttps) {
      if (!palaSecureConnect(secureClient, p.host.c_str(), p.port, 12000)) continue;
      client = &secureClient;
    } else {
      plainClient.setTimeout(12);
      if (!plainClient.connect(p.host.c_str(), p.port)) continue;
      client = &plainClient;
    }

    client->printf("PATCH %s/api/v1/%s?updateMask=content HTTP/1.1\r\n"
                   "Host: %s\r\n"
                   "Authorization: Bearer %s\r\n"
                   "Content-Type: application/json\r\n"
                   "Content-Length: %u\r\n"
                   "Connection: close\r\n\r\n",
                   p.basePath.c_str(), t.memoName, p.host.c_str(), token.c_str(), (unsigned)patchPayload.length());
    client->print(patchPayload);

    deadline = millis() + 10000;
    while (!client->available() && millis() < deadline) delay(20);
    client->stop();

    t.dirty = false;
    Serial.printf("[Tasks] Synced task \"%s\" (%s) to %s\n", t.text, t.done ? "DONE" : "TODO", t.memoName);
  }

  saveTasksToSD();
  return true;
}
