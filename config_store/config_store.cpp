#include "Arduino.h"
#include "config_store.h"
#include "../../config.h"
#include "../../secrets_inc.h"
#include "SD_MMC.h"
#include "rtc.h"

static const char* CONFIG_PATH = "/notes/config.txt";

static String s_wifiSsid    = WIFI_SSID;
static String s_wifiPass    = WIFI_PASS;
static String s_memosUrl    = MEMOS_INSTANCE_URL;
static String s_memosToken  = MEMOS_ACCESS_TOKEN;
static String s_memosVis    = MEMOS_VISIBILITY;
static int    s_tzOffset    = LOCAL_TIME_OFFSET_MIN;
static String s_portalPin   = "";

void configLoad() {
  if (!SD_MMC.exists(CONFIG_PATH)) {
    configSave();
    return;
  }

  File f = SD_MMC.open(CONFIG_PATH);
  if (!f) return;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.startsWith("#") || !line.length()) continue;

    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    val.trim();

    if (key.equalsIgnoreCase("wifi_ssid") && val.length())           s_wifiSsid = val;
    else if (key.equalsIgnoreCase("wifi_pass") && val.length())      s_wifiPass = val;
    else if (key.equalsIgnoreCase("memos_url") && val.length())      s_memosUrl = val;
    else if (key.equalsIgnoreCase("memos_token") && val.length())    s_memosToken = val;
    else if (key.equalsIgnoreCase("memos_vis") && val.length())      s_memosVis = val;
    else if (key.equalsIgnoreCase("tz_offset_min"))                  s_tzOffset = val.toInt();
    else if (key.equalsIgnoreCase("portal_pin"))                     s_portalPin = val;
  }
  f.close();

  if (s_wifiSsid == "YOUR_WIFI_SSID" || s_wifiSsid.length() == 0) {
    s_wifiSsid = WIFI_SSID;
    configSave();
  }
  if (s_wifiPass.length() == 0) {
    s_wifiPass = WIFI_PASS;
    configSave();
  }
  if (s_memosUrl.length() == 0 || s_memosUrl.indexOf("example.com") >= 0) {
    s_memosUrl = MEMOS_INSTANCE_URL;
    configSave();
  }
  if (s_memosToken.length() == 0 || s_memosToken == "your_pat_token_here") {
    s_memosToken = MEMOS_ACCESS_TOKEN;
    configSave();
  }
}

void configSave() {
  const char* tmp = "/notes/config.tmp";
  if (SD_MMC.exists(tmp)) SD_MMC.remove(tmp);

  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return;

  f.printf("wifi_ssid=%s\n", s_wifiSsid.c_str());
  f.printf("wifi_pass=%s\n", s_wifiPass.c_str());
  f.printf("memos_url=%s\n", s_memosUrl.c_str());
  f.printf("memos_token=%s\n", s_memosToken.c_str());
  f.printf("memos_vis=%s\n", s_memosVis.c_str());
  f.printf("tz_offset_min=%d\n", s_tzOffset);
  f.printf("portal_pin=%s\n", s_portalPin.c_str());
  f.close();

  if (SD_MMC.exists(CONFIG_PATH)) SD_MMC.remove(CONFIG_PATH);
  SD_MMC.rename(tmp, CONFIG_PATH);
}

void configInit() {
  configLoad();
  rtcApplyTimezone();
}

String configGetWifiSsid()           { return s_wifiSsid; }
String configGetWifiPass()           { return s_wifiPass; }
String configGetMemosUrl()           { return s_memosUrl; }
String configGetMemosToken()         { return s_memosToken; }
String configGetMemosVisibility()    { return s_memosVis.length() ? s_memosVis : "PRIVATE"; }
int    configGetTimezoneOffsetMin()  { return s_tzOffset; }
String configGetPortalPin()          { return s_portalPin; }

void configSet(const String& ssid, const String& pass, const String& memosUrl, const String& memosToken,
               const String& memosVis, int tzOffsetMin, const String& pin) {
  if (ssid.length())       s_wifiSsid = ssid;
  s_wifiPass = pass;
  if (memosUrl.length())   s_memosUrl = memosUrl;
  if (memosToken.length()) s_memosToken = memosToken;
  if (memosVis.length())   s_memosVis = memosVis;
  s_tzOffset = tzOffsetMin;
  s_portalPin = pin;
  configSave();
  rtcApplyTimezone();
}
