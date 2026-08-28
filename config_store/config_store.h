#pragma once
#include <Arduino.h>

void   configInit();
void   configLoad();
void   configSave();

String configGetWifiSsid();
String configGetWifiPass();
String configGetMemosUrl();
String configGetMemosToken();
String configGetMemosVisibility();
int    configGetTimezoneOffsetMin();
String configGetPortalPin();

void   configSet(const String& ssid, const String& pass, const String& memosUrl, const String& memosToken,
                 const String& memosVis = "PRIVATE", int tzOffsetMin = 120, const String& pin = "");
