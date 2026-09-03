#pragma once
#include <Arduino.h>
#include "master_config.h"

// ---------------------------------------------------------------------------
// LoRa side of the gateway. Same wire frame as the node:
//   "<dst>,<src>,<seq>,<cmd>[,<arg>...]" + CRC32(4B LE) + '\n'
// The gateway round-robins RD across the adopted node table, and can push
// WR/WP writes queued from the Modbus side. It also runs discovery (DISC) and
// adoption (ADOPT/RELEASE) on request from the captive portal.
// ---------------------------------------------------------------------------

struct NodeSnapshot {
  bool     online;
  uint8_t  misses;
  int16_t  rssi;
  uint32_t lastReplyMs;
  uint16_t ai[4];
  uint8_t  di[4];        // 0/1
  char     ro[4];        // '0' / '1' / 'x'
};
extern NodeSnapshot snap[MASTER_MAX_NODES];

struct DiscoveredNode { char mac[13]; char fw[12]; };
extern DiscoveredNode discovered[MASTER_MAX_NODES];
extern uint8_t        discoveredCount;

bool masterBegin();       // radio.begin() from mcfg + continuous RX
void masterStandby();     // radio.standby()
void masterPollLoop();    // non-blocking: RD / queued WR-WP + reply handling

void masterQueueRelays(int slot, const char want[4]);       // want chars: '0' '1' '-'
void masterQueuePulse(int slot, uint8_t idx1, uint16_t ms);

// Blocking helpers, called from the portal (poll loop is idle in portal mode).
uint8_t masterDiscover(uint16_t windowMs = 4000);           // fills discovered[], returns count
bool    masterAdopt(const char* mac, uint8_t addr, const char* name);
bool    masterRelease(int slot);

// ROLLCALL: reconstruye la tabla de nodos desde el campo (nodos ya adoptados que
// responden HERE). Uso: al arrancar con la tabla vacia, o desde el portal.
// Devuelve cuantos nodos se incorporaron. Persiste si incorpora alguno.
uint8_t masterRollcall(uint16_t windowMs = 4000);
