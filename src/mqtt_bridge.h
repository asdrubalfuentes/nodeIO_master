#pragma once
#include <Arduino.h>

// Puente MQTT del gateway.  Publica toda la orquestacion (MAPA A de los nodos,
// espejo de MAPA B que escribe el LOGO!, salud del gateway) y acepta comandos
// desde la nube hacia los coils de MAPA G.
// Contrato: ORCHESTRATION/MQTT_BRIDGE.md  +  REGISTER_MAP.md SS7.
//
// Requiere WiFi STA arriba (net_master) y, para el `ts`, SNTP (net_master).

void mqttBridgeBegin();     // configura el cliente; no conecta
void mqttBridgeLoop();      // reconexion no bloqueante + loop() + publicadores
bool mqttBridgeConnected();

// Acciones a nivel gateway pedidas por MQTT (`.../gw/cmd`); el main las atiende
// fuera del callback para no bloquear.  Devuelven y limpian el flag.
bool mqttTakeWantRollcall();
bool mqttTakeWantOta();
