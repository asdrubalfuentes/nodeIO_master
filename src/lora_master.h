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

  // Escalado/totalizador/alarma que ya trae el nodo (PROTO_FW >= 1.2026.007;
  // cambio de rumbo 2026-09). En un nodo con firmware viejo se quedan en 0 --
  // parseStatus() no los toca si el frame no trae esos campos.
  int16_t  eng[2];       // 0=nivel, 1=caudal, x100, ya escalado y filtrado
  int32_t  accDia[2];    // acumulado del dia en curso, m3 x1000
  int32_t  accMes[2];    // acumulado del mes en curso, m3 x1000
  uint8_t  almBits;      // bit0 nivel.almLo · bit1 nivel.almHi · bit2 caudal.almLo · bit3 caudal.almHi
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

// Cierre de dia/mes del totalizador del nodo (el nodo no tiene hora propia;
// el gateway, que si la tiene via SNTP, dispara esto en el momento justo).
// mask: bit0=nivel bit1=caudal, por defecto ambos. Encolado, no bloqueante --
// se envia en el proximo turno de sondeo de ese nodo, igual que WR/WP.
void masterQueueCloseDay(int slot, uint8_t mask = 0x03);
void masterQueueCloseMonth(int slot, uint8_t mask = 0x03);

// Blocking helpers, called from the portal (poll loop is idle in portal mode).
uint8_t masterDiscover(uint16_t windowMs = 4000);           // fills discovered[], returns count
bool    masterAdopt(const char* mac, uint8_t addr, const char* name);
bool    masterRelease(int slot);

// ROLLCALL: reconstruye la tabla de nodos desde el campo (nodos ya adoptados que
// responden HERE). Uso: al arrancar con la tabla vacia, o desde el portal.
// Devuelve cuantos nodos se incorporaron. Persiste si incorpora alguno.
uint8_t masterRollcall(uint16_t windowMs = 4000);

// OTA: pide al nodo `slot` que reinicie en modo actualizacion (descarga el
// firmware de GitHub Releases por su WiFi de mantenimiento). Devuelve true si el
// nodo confirmo (ACK). Bloqueante; llamar desde el portal.
bool masterOtaTrigger(int slot);
