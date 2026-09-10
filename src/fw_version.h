#pragma once

// Cadena descriptiva (OLED / log).
#define FW_VERSION_STR "V1.2026.006-gw"

// Version semver (X.Y.Z) del canal OTA (GitHub Releases). El CI la sobreescribe
// desde el tag vX.Y.Z; sin CI vale este literal.
//   1.2.0  OTA
//   1.4.0  puente MQTT + identidad NVS separada (sobrevive a bumps de CFG_MAGIC)
#ifdef FW_VERSION_OVERRIDE
#  define FW_SEMVER FW_VERSION_OVERRIDE
#else
#  define FW_SEMVER "1.4.0"
#endif
