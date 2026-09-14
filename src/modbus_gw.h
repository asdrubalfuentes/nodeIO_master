#pragma once
#include <Arduino.h>

// Servidor Modbus que expone la IO de los nodos remotos.  MAPA A del contrato
// ORCHESTRATION/REGISTER_MAP.md (CONTRACT_VERSION 1).  Transporte segun
// mcfg.mbTransport: MBT_TCP (por defecto, :502 sobre WiFi STA), MBT_RTU
// (RS-485/USB, respaldo de banco) o MBT_BOTH.  Unit ID = mcfg.mbSlaveId.
//
// Por nodo i = 0..7, base = i*16:
//   Input Reg (FC04)  b+0..3  AI1..AI4 (0..4095)   [b+0 Nivel, b+1 Caudal]
//                     b+4     DI bitfield (bit0 presostato, bit1 volt local,
//                             bit2 tamper tapa, bit3 reserva)
//                     b+5     rele bitfield (bit0..3 estado, bit8..11 'x'=deshab.)
//                     b+6     link (0/1)
//                     b+7     RSSI dBm (int16)
//                     b+8     s desde la ultima respuesta
//                     b+9     direccion LoRa asignada (0 = vacio)
//                     b+10    FW del nodo
//   Discrete In (FC02) b+0..3 DI1..DI4 ; b+4 link online
//   Coil (FC01/05/15)  b+0..3 consigna de rele RO1..RO4 (1 = cerrar; RO1 sirena)
//                      b+4..7 disparo de pulso RO1..RO4 (1 -> WP, auto-limpia)
//
// Global Input Reg: 900 marca 0x0203, 901 nodeCount, 902 online, 903 localIoEnabled
// IO local (si aplica): Ireg 904..907 AI, 908 DI bits, 909 rele bits ; Coil 900..903
//
// MAPA A2 (cambio de rumbo 2026-09): escalado/totalizador/alarma que ya trae
// el nodo -- PROTO_FW >= 1.2026.007. Input Reg, por nodo i=0..7, base 200+i*16
// (200, no 1000: ese numero ya lo usan los COILS de MAPA G -- direcciones de
// objeto distinto, no chocarian, pero se presta a confusion en la doc/HMI):
//   c+0   nivel escalado x100 (int16)      c+1   caudal escalado x100 (int16)
//   c+2/3 acumulado dia nivel, m3 x1000 (int32, palabra alta primero)
//   c+4/5 acumulado mes nivel, m3 x1000
//   c+6/7 acumulado dia caudal, m3 x1000
//   c+8/9 acumulado mes caudal, m3 x1000
//   c+10  almBits (bit0 nivel.almLo b1 nivel.almHi b2 caudal.almLo b3 caudal.almHi)
#define MAPA2_BASE    200
#define MAPA2_STRIDE  16

void modbusBegin();
void modbusTask();      // llamar en cada loop(): task() de los backends + publish throttled
bool modbusTcpReady();  // true cuando el servidor Modbus TCP esta escuchando

// ---- MAPA G: puente hacia MQTT (solo backend TCP) --------------------------
// El LOGO! escribe el espejo de MAPA B (Holding Regs 0..105) con Network Output
// y lee los coils de comando (1000 + s*16 + k) con Network Input.
// Ver ORCHESTRATION/REGISTER_MAP.md SS7 y MQTT_BRIDGE.md.
#define MAPG_HR_BASE     0
#define MAPG_HR_COUNT    106       // estaciones (0..63) + bloque global (96..105)
#define MAPG_CMD_BASE    1000
#define MAPG_CMD_STRIDE  16
#define MAPG_CMD_COUNT   32        // 2 estaciones

uint16_t modbusMirrorHreg(uint16_t addr);   // lee el espejo (0 si el TCP no esta listo)
// Pone un coil de comando de MAPA G (dir. absoluta 1000..1031). pulse=true ->
// se auto-limpia tras un tiempo (para que el flanco llegue al LOGO! una vez).
void     modbusCmdCoil(uint16_t coilAbs, bool value, bool pulse);
