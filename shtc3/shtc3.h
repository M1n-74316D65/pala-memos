#pragma once
#include <Arduino.h>

bool   shtc3Init();
bool   shtc3Read(float& tempC, float& humidityPercent);
String shtc3Label();
