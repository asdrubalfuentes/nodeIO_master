#pragma once
#include <Arduino.h>
#include "master_config.h"

// When Modbus RTU runs on the USB UART (mcfg.mbUsb), the debug console shares
// that same wire, so every log write must be suppressed or it corrupts the
// Modbus frames the Python master is reading. Route all diagnostics through
// these macros; they go quiet automatically in USB-Modbus mode.
#define LOGF(...)  do { if (!mcfg.mbUsb) Serial.printf(__VA_ARGS__); } while (0)
#define LOGLN(s)   do { if (!mcfg.mbUsb) Serial.println(s);          } while (0)
