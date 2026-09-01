#pragma once
#include <Arduino.h>

#define MASTER_MAX_NODES  8

struct MasterNode {
  uint8_t addr;         // LoRa address assigned to this node (0 = empty slot)
  char    mac[13];      // node idUnico (12 hex) + NUL
  char    name[16];
  bool    enabled;
};

// Persistent gateway configuration (NVS namespace "masterio", one blob + magic).
struct MasterConfig {
  uint8_t  masterLoraAddr;              // this gateway's LoRa src address (default 200)

  // --- Modbus RTU (serial) ---
  uint8_t  mbSlaveId;                   // default 1
  uint32_t mbBaud;                      // default 19200
  uint8_t  mbFormat;                    // 0=8N1 1=8E1 2=8O1 3=8N2 4=8E2 5=8O2  (default 1)
  int8_t   mbRxPin, mbTxPin, mbDePin;   // RS-485 mode: default 2, 3, 4 ; DE = -1 -> no direction control
  bool     mbUsb;                       // true -> Modbus runs on Serial (UART0, GPIO43/44 = USB);
                                        //         console logging is suppressed, DE ignored. (default true)

  // --- LoRa channel (authoritative; pushed to nodes on ADOPT) ---
  float    loraFreq, loraBw;            // 915.0 / 125.0
  uint8_t  loraSf, loraCr, loraSync;    // 9 / 5 / 0x34
  int8_t   loraPwr;                     // 14

  // --- polling ---
  uint16_t pollMs;                      // spacing between node polls (default 250)
  uint16_t ackTimeoutMs;               // wait for a node reply (default 500)
  uint8_t  offlineAfter;              // consecutive misses before OFFLINE (default 3)
  uint16_t pulseMs;                    // WP width for a Modbus pulse-trigger coil (default 500)

  // --- local carrier IO (disabled by default; master is a pure gateway) ---
  bool     localIoEnabled;

  // --- captive portal SoftAP ---
  char     apSsid[24];                 // "MasterIO-Setup"
  char     apPass[24];                 // "aysafi1234"

  // --- adopted node table ---
  uint8_t    nodeCount;               // number of populated slots (0..MASTER_MAX_NODES)
  MasterNode nodes[MASTER_MAX_NODES];
};

extern MasterConfig mcfg;

void masterConfigFactory();
void masterConfigLoad();
bool masterConfigSave();
bool masterConfigStored();

int  masterFindByMac(const char* mac);   // slot index or -1
int  masterFindByAddr(uint8_t addr);     // slot index or -1
bool masterAddrFree(uint8_t addr);       // true if no enabled slot uses it
int  masterAddNode(uint8_t addr, const char* mac, const char* name);  // slot or -1 (full)
void masterRemoveNode(int slot);

uint32_t mbFormatToConfig(uint8_t f);    // -> SERIAL_8x1 constant
String   masterMac();                    // gateway's own efuse MAC (12 hex)
