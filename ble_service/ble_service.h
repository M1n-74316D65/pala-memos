#pragma once
#include <Arduino.h>

void bleServiceInit();
void bleServiceStart();
void bleServiceStop();
bool bleIsActive();
bool bleIsConnected();
void bleNotifyStatus();
