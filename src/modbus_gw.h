#pragma once
#include <Arduino.h>

// Modbus RTU slave that exposes the remote nodes' IO.
//
// Per node slot i = 0..7, base = i*16:
//   Input Reg (FC04)  b+0..3  AI1..AI4 (0..4095)
//                     b+4     DI bitfield (bit0..3)
//                     b+5     relay bitfield (bit0..3 state, bit8..11 disabled 'x')
//                     b+6     link (0/1)
//                     b+7     RSSI dBm (int16)
//                     b+8     seconds since last reply
//                     b+9     assigned LoRa address (0 = empty)
//   Discrete In (FC02) b+0..3 DI1..DI4 ; b+4 link online
//   Coil (FC01/05/15)  b+0..3 relay setpoint RO1..RO4 (write 1 = close)
//                      b+4..7 pulse trigger RO1..RO4 (write 1 -> WP, auto-clears)
//
// Global Input Reg: 900 marker, 901 nodeCount, 902 online, 903 localIoEnabled
// Local IO (if enabled): Ireg 904..907 AI, 908 DI bits, 909 relay bits ; Coil 900..903

void modbusBegin();
void modbusTask();     // call every loop(): mb.task() + throttled register publish
