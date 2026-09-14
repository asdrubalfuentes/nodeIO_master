#pragma once

// Cadena descriptiva (OLED / log).
#define FW_VERSION_STR "V1.2026.007-gw"

// Version semver (X.Y.Z) del canal OTA (GitHub Releases). El CI la sobreescribe
// desde el tag vX.Y.Z; sin CI vale este literal.
//   1.2.0  OTA
//   1.4.0  puente MQTT + identidad NVS separada (sobrevive a bumps de CFG_MAGIC)
//   1.5.0  MAPA A2 (escalado/acumulados/alarma de los nodos) + cierre automatico
//          de dia/mes (SNTP) + F2 fuerza chequeo OTA
//   1.5.1  fix: ackTimeoutMs por defecto 500ms->2000ms. La trama ST v3 (escalado+
//          acumulados+alarma, ~110-130 bytes) tarda ~650-700ms de aire a SF9/BW125;
//          con 500ms el gateway declaraba timeout ANTES de que la respuesta del
//          nodo terminara de llegar -> nodo adoptado pero SIEMPRE offline en el
//          sondeo normal (el ADOPT, con trama ACK corta, si alcanzaba a tiempo).
//          Equipos ya en campo: subir "Timeout ACK ms" a 2000 en el portal, no
//          hace falta reflashear.
#ifdef FW_VERSION_OVERRIDE
#  define FW_SEMVER FW_VERSION_OVERRIDE
#else
#  define FW_SEMVER "1.5.1"
#endif
