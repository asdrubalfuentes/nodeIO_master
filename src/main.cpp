#include <Arduino.h>
#include <heltec_unofficial.h>
#include "images.h"
#include "io.h"
#include "master_config.h"
#include "lora_master.h"
#include "modbus_gw.h"
#include "net_master.h"
#include "portal_master.h"
#include "log.h"

#define FW_VERSION "V1.2026.006-gw"   // + Modbus TCP/WiFi STA + ROLLCALL

enum Mode { MODE_NORMAL, MODE_PORTAL, MODE_MENU, MODE_NODE_VIEW };
static Mode     mode            = MODE_NORMAL;
static uint32_t btn1DownSince   = 0;
static uint32_t btn2DownSince   = 0;
static uint32_t btnBuiltinDownSince = 0;
static uint32_t lastDrawMs      = 0;
static int      selectedNode    = 0;

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
  portalStart();
}

static void enterMenu() {
  mode = MODE_MENU;
  selectedNode = 0;
  lastDrawMs = 0;
}

static void enterNodeView(int nodeSlot) {
  if (nodeSlot < 0 || nodeSlot >= MASTER_MAX_NODES) return;
  mode = MODE_NODE_VIEW;
  selectedNode = nodeSlot;
  lastDrawMs = 0;
}

static void exitToNormal() {
  mode = MODE_NORMAL;
  lastDrawMs = 0;
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
  if (mcfg.mbTransport == MBT_RTU) {
    snprintf(l, sizeof(l), "Modbus RTU %u @ %lu %s", mcfg.mbSlaveId,
             (unsigned long)mcfg.mbBaud,
             mcfg.mbUsb ? "usb" : (mcfg.mbDePin >= 0 ? "485" : "ttl"));
  } else if (netStaUp()) {
    snprintf(l, sizeof(l), "TCP %s:%u", netStaIp().c_str(),
             mcfg.mbTcpPort ? mcfg.mbTcpPort : 502);
  } else {
    snprintf(l, sizeof(l), "TCP :%u  wifi...",
             mcfg.mbTcpPort ? mcfg.mbTcpPort : 502);
  }
  display.drawString(0, 28, l);
  if (lastAddr) snprintf(l, sizeof(l), "ult. addr %u rssi %d", lastAddr, lastRssi);
  else          snprintf(l, sizeof(l), "sin respuestas aun");
  display.drawString(0, 42, l);
  display.display();
}

static void drawMenuScreen() {
  if (millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "MENU NODOS");

  char l[48];
  int adoptedCount = 0, adoptedNodes[MASTER_MAX_NODES];
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    if (mcfg.nodes[i].addr && mcfg.nodes[i].enabled) {
      adoptedNodes[adoptedCount++] = i;
    }
  }

  if (adoptedCount == 0) {
    display.drawString(0, 20, "Sin nodos adoptados");
    display.display();
    return;
  }

  int menuPos = 0;
  for (int j = 0; j < adoptedCount; j++) {
    if (adoptedNodes[j] == selectedNode) { menuPos = j; break; }
  }

  snprintf(l, sizeof(l), "%s Global", menuPos == -1 ? ">" : " ");
  display.drawString(0, 14, l);

  for (int j = 0; j < adoptedCount && j < 3; j++) {
    int i = adoptedNodes[j];
    snprintf(l, sizeof(l), "%s Nodo %u", (menuPos == j) ? ">" : " ", mcfg.nodes[i].addr);
    display.drawString(0, 28 + j * 12, l);
  }

  display.drawString(0, 56, "[F1+F2]=Sel [F2]=Exit");
  display.display();
}

static void drawNodeViewScreen() {
  if (millis() - lastDrawMs < 250) return;
  lastDrawMs = millis();

  display.clear();
  display.setFont(ArialMT_Plain_10);

  char l[48];
  snprintf(l, sizeof(l), "Nodo %u", mcfg.nodes[selectedNode].addr);
  display.drawString(0, 0, l);

  NodeSnapshot& s = snap[selectedNode];

  snprintf(l, sizeof(l), "AI: %u %u %u %u", s.ai[0], s.ai[1], s.ai[2], s.ai[3]);
  display.drawString(0, 12, l);

  snprintf(l, sizeof(l), "DI: %u %u %u %u", s.di[0], s.di[1], s.di[2], s.di[3]);
  display.drawString(0, 24, l);

  snprintf(l, sizeof(l), "RO: %c %c %c %c", s.ro[0], s.ro[1], s.ro[2], s.ro[3]);
  display.drawString(0, 36, l);

  snprintf(l, sizeof(l), "RSSI: %d | %s", s.rssi, s.online ? "ON" : "OFF");
  display.drawString(0, 48, l);

  display.drawString(0, 56, "[F1/F2]=Nav [F2L]=Menu");
  display.display();
}

static void handleButtonsNormal() {
  bool btn1_pressed = digitalRead(PIN_BUTTON_1) == LOW;
  bool btn2_pressed = digitalRead(PIN_BUTTON_2) == LOW;
  bool btn_builtin_pressed = digitalRead(PIN_BUTTON_BUILTIN) == LOW;

  if (btn_builtin_pressed) {
    if (btnBuiltinDownSince == 0) btnBuiltinDownSince = millis();
    else if (millis() - btnBuiltinDownSince > 5000) {
      enterPortal();
      return;
    }
  } else {
    btnBuiltinDownSince = 0;
  }

  if (btn1_pressed) {
    if (btn1DownSince == 0) btn1DownSince = millis();
    else if (millis() - btn1DownSince > 5000 && millis() - btn1DownSince < 5100) {
      // short press detected (released and re-checked to avoid double-trigger)
    }
  } else {
    if (btn1DownSince > 0 && millis() - btn1DownSince < 5000) {
      enterMenu();
    }
    btn1DownSince = 0;
  }
}

static void handleButtonsMenu() {
  bool btn1_pressed = digitalRead(PIN_BUTTON_1) == LOW;
  bool btn2_pressed = digitalRead(PIN_BUTTON_2) == LOW;

  if (btn1_pressed) {
    if (btn1DownSince == 0) btn1DownSince = millis();
  } else {
    btn1DownSince = 0;
  }

  if (btn2_pressed) {
    if (btn2DownSince == 0) btn2DownSince = millis();
    else if (millis() - btn2DownSince > 3000) {
      exitToNormal();
      return;
    }
  } else {
    if (btn2DownSince > 0 && millis() - btn2DownSince < 500) {
      exitToNormal();
    }
    btn2DownSince = 0;
  }

  // Both buttons = enter node view
  if (btn1_pressed && btn2_pressed) {
    enterNodeView(selectedNode);
    return;
  }

  // Collect adopted nodes
  int adoptedCount = 0, adoptedNodes[MASTER_MAX_NODES];
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    if (mcfg.nodes[i].addr && mcfg.nodes[i].enabled) {
      adoptedNodes[adoptedCount++] = i;
    }
  }

  if (adoptedCount == 0) return;

  // Find current position
  int menuPos = -1;
  for (int j = 0; j < adoptedCount; j++) {
    if (adoptedNodes[j] == selectedNode) { menuPos = j; break; }
  }

  // F1 only = prev node
  if (btn1_pressed && !btn2_pressed && btn1DownSince > 0 && millis() - btn1DownSince > 300) {
    if (menuPos >= 0) {
      menuPos = (menuPos - 1 + adoptedCount) % adoptedCount;
    } else {
      menuPos = adoptedCount - 1;
    }
    selectedNode = adoptedNodes[menuPos];
    btn1DownSince = millis();
  }

  // F2 only = next node
  if (btn2_pressed && !btn1_pressed && btn2DownSince > 0 && millis() - btn2DownSince > 300 && millis() - btn2DownSince < 3000) {
    if (menuPos >= 0) {
      menuPos = (menuPos + 1) % adoptedCount;
    } else {
      menuPos = 0;
    }
    selectedNode = adoptedNodes[menuPos];
    btn2DownSince = millis();
  }
}

static void handleButtonsNodeView() {
  bool btn1_pressed = digitalRead(PIN_BUTTON_1) == LOW;
  bool btn2_pressed = digitalRead(PIN_BUTTON_2) == LOW;

  if (btn1_pressed) {
    if (btn1DownSince == 0) btn1DownSince = millis();
  } else {
    btn1DownSince = 0;
  }

  if (btn2_pressed) {
    if (btn2DownSince == 0) btn2DownSince = millis();
    else if (millis() - btn2DownSince > 3000) {
      enterMenu();
      return;
    }
  } else {
    btn2DownSince = 0;
  }

  // Collect adopted nodes
  int adoptedCount = 0, adoptedNodes[MASTER_MAX_NODES];
  for (int i = 0; i < MASTER_MAX_NODES; i++) {
    if (mcfg.nodes[i].addr && mcfg.nodes[i].enabled) {
      adoptedNodes[adoptedCount++] = i;
    }
  }

  if (adoptedCount == 0) {
    exitToNormal();
    return;
  }

  // Find current position
  int nodePos = -1;
  for (int j = 0; j < adoptedCount; j++) {
    if (adoptedNodes[j] == selectedNode) { nodePos = j; break; }
  }

  // F1 = prev node
  if (btn1_pressed && !btn2_pressed && btn1DownSince > 0 && millis() - btn1DownSince > 300) {
    nodePos = (nodePos - 1 + adoptedCount) % adoptedCount;
    selectedNode = adoptedNodes[nodePos];
    btn1DownSince = millis();
  }

  // F2 (short) = next node
  if (btn2_pressed && !btn1_pressed && btn2DownSince > 0 && millis() - btn2DownSince > 300 && millis() - btn2DownSince < 3000) {
    nodePos = (nodePos + 1) % adoptedCount;
    selectedNode = adoptedNodes[nodePos];
    btn2DownSince = millis();
  }
}

void setup() {
  heltec_setup();
  masterConfigLoad();
  LOGLN("\nMaster IO gateway, by Aysafi " FW_VERSION);

  ioInit(0x00, 0x00);
  splash();

  if (!masterBegin()) {
    LOGLN("[lora] fallo de init -> portal");
    enterPortal();
    return;
  }

  if (mcfg.nodeCount == 0) {
    // Tabla vacia (primer arranque, NVS borrada o firmware nuevo): antes de ir
    // al portal, intenta reconstruirla desde el campo con ROLLCALL.
    LOGLN("[cfg] sin nodos -> ROLLCALL");
    if (masterRollcall() == 0) {
      LOGLN("[cfg] ROLLCALL vacio -> portal");
      enterPortal();
      return;
    }
    LOGF("[cfg] ROLLCALL recupero %u nodo(s)\n", mcfg.nodeCount);
  }

  netBegin();            // WiFi STA (si esta habilitada) para Modbus TCP
  modbusBegin();
  mode = MODE_NORMAL;
}

void loop() {
  heltec_loop();

  if (mode == MODE_NORMAL) {
    handleButtonsNormal();
    netLoop();
    masterPollLoop();
    modbusTask();
    drawStatusScreen();
  }
  else if (mode == MODE_PORTAL) {
    portalLoop();
    drawPortalScreen();
  }
  else if (mode == MODE_MENU) {
    handleButtonsMenu();
    drawMenuScreen();
  }
  else if (mode == MODE_NODE_VIEW) {
    handleButtonsNodeView();
    drawNodeViewScreen();
  }
}
