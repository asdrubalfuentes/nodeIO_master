#include <Arduino.h>
#include <heltec_unofficial.h>   // radio (SX1262), display (SSD1306Wire), button (HotButton)
#include "images.h"
#include "io.h"
#include "master_config.h"
#include "lora_master.h"
#include "modbus_gw.h"
#include "portal_master.h"
#include "log.h"

// Master IO - by Aysafi
// LoRa <-> Modbus RTU gateway. Polls up to 8 adopted nodeIO nodes over LoRa and
// exposes their analog/digital inputs (and relay outputs) as Modbus registers.
// Node discovery + adoption is done from the captive portal.
#define FW_VERSION "V1.2026.003-gw"

enum Mode { MODE_NORMAL, MODE_PORTAL };
static Mode     mode          = MODE_NORMAL;
static uint32_t btn1DownSince = 0;
static uint32_t lastDrawMs    = 0;

// ---------------------------------------------------------------------------
static void splash() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "Heltec LoRa V3");
  display.drawString(0, 20, "Aysafi " FW_VERSION);
  display.drawString(0, 40, "LoRa <-> Modbus GW");
  display.display();
  delay(1500);
  display.clear();
  display.drawXbm(0, 8, poweredBy_width, poweredBy_height, poweredBy);
  display.display();
  delay(1500);
}

static void enterPortal() {
  mode = MODE_PORTAL;
  portalStart();               // LoRa stays up for discovery/adoption
}

static void drawPortalScreen() {
  if (millis() - lastDrawMs < 500) return;
  lastDrawMs = millis();
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0,  "MODO CONFIG");
  display.drawString(0, 16, String("SSID: ") + mcfg.apSsid);
  display.drawString(0, 30, String("IP: ") + portalIP());
  display.drawString(0, 46, "Descubrir / adoptar nodos");
  display.display();
}

static void drawStatusScreen() {
  if (millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();

  uint8_t online = 0, lastAddr = 0; int16_t lastRssi = 0; uint32_t newest = 0;
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    if (!mcfg.nodes[i].addr || !mcfg.nodes[i].enabled) continue;
    if (snap[i].online) online++;
    if (snap[i].lastReplyMs > newest) { newest = snap[i].lastReplyMs; lastAddr = mcfg.nodes[i].addr; lastRssi = snap[i].rssi; }
  }

  char l[48];
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "MASTER IO / gateway");
  snprintf(l, sizeof(l), "Nodos online %u/%u", online, mcfg.nodeCount);
  display.drawString(0, 14, l);
  snprintf(l, sizeof(l), "Modbus %u @ %lu %s", mcfg.mbSlaveId,
           (unsigned long)mcfg.mbBaud,
           mcfg.mbUsb ? "usb" : (mcfg.mbDePin >= 0 ? "485" : "ttl"));
  display.drawString(0, 28, l);
  if (lastAddr) snprintf(l, sizeof(l), "ult. addr %u rssi %d", lastAddr, lastRssi);
  else          snprintf(l, sizeof(l), "sin respuestas aun");
  display.drawString(0, 42, l);
  display.display();
}

// ---------------------------------------------------------------------------
void setup() {
  heltec_setup();
  masterConfigLoad();
  LOGLN("\nMaster IO gateway, by Aysafi " FW_VERSION);

  ioInit(0x00, 0x00);          // buttons + safe relays; local IO read only if enabled
  splash();

  if (mcfg.nodeCount == 0) {
    LOGLN("[cfg] sin nodos -> portal");
    masterBegin();             // radio up so discovery works from the portal
    enterPortal();
  } else if (!masterBegin()) {
    LOGLN("[lora] fallo de init -> portal");
    enterPortal();
  } else {
    modbusBegin();
    mode = MODE_NORMAL;
  }
}

void loop() {
  heltec_loop();

  if (mode == MODE_NORMAL) {
    if (digitalRead(PIN_BUTTON_1) == LOW) {
      if (btn1DownSince == 0) btn1DownSince = millis();
      else if (millis() - btn1DownSince > 3000) { enterPortal(); return; }
    } else {
      btn1DownSince = 0;
    }
  }

  if (mode == MODE_PORTAL) {
    portalLoop();
    drawPortalScreen();
    return;
  }

  masterPollLoop();
  modbusTask();
  drawStatusScreen();
}
