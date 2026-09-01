#pragma once
#include <Arduino.h>

// Captive configuration portal for the gateway. LoRa stays up while the portal
// runs so node discovery / adoption can use the radio.
extern bool portalActive;

void   portalStart();
void   portalStop();
void   portalLoop();
String portalIP();
