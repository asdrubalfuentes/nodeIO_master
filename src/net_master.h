#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// WiFi STA del gateway: lo une al segmento de planta para servir Modbus TCP
// (MAPA A del contrato). Independiente de la radio LoRa (SX1262 por SPI).
// El portal cautivo sigue usando SoftAP; STA solo vive en MODO NORMAL.
// ---------------------------------------------------------------------------

void     netBegin();      // arranca STA si mcfg.staEnabled; no bloquea
void     netLoop();       // FSM de reconexion + SNTP; llamar desde loop()
bool     netStaUp();      // true cuando hay IP en el segmento de planta
String   netStaIp();      // "0.0.0.0" si no conectado

bool     netTimeOk();     // true cuando SNTP ya sincronizo
uint32_t netEpoch();      // epoca UNIX en s (0 si sin hora)
