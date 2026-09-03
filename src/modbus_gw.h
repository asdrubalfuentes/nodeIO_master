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

void modbusBegin();
void modbusTask();      // llamar en cada loop(): task() de los backends + publish throttled
bool modbusTcpReady();  // true cuando el servidor Modbus TCP esta escuchando
