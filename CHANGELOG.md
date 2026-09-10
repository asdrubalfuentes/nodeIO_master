# Changelog — nodeIO_master

Formato de versión del canal OTA: `MAJOR.MINOR.PATCH` (semver numérico).
El firmware embebe `FW_SEMVER`; el CI lo sobreescribe desde el tag `vX.Y.Z`.

## 1.4.0 — puente MQTT + identidad NVS separada

### Identidad separada (sobrevive a los bumps de `CFG_MAGIC`)

- La **tabla de nodos**, el **canal LoRa**, la **dirección del master**, la
  **WiFi de planta** y la **config MQTT** se guardan como **claves sueltas** en
  NVS (sin `magic`). Desde aquí, subir `CFG_MAGIC` para meter features nuevas
  **ya no borra el emparejamiento**.
- **Red de seguridad**: espejo de la identidad de campo (tabla + canal + master
  addr) en **LittleFS** (`/id.bin`, partición aparte). Se restaura solo si la
  identidad de NVS aparece en blanco.
- `CFG_MAGIC` 03 → **04**. ⚠️ **Este bump reinicia el emparejamiento una última
  vez** (el firmware anterior no escribía las claves de identidad). Tras
  actualizar a 1.4.0: si los nodos están en el canal de fábrica, el `ROLLCALL`
  del arranque reconstruye la tabla; si usan canal propio, re-adóptalos. **A
  partir de 1.4.0 queda protegido.** Recomendado: 1.4.0 = último flasheo por USB.

### Puente MQTT (`MQTT_BRIDGE.md`)

- **`modbus_gw`**: bloque nuevo en el servidor TCP — **Holding Registers 0..105**
  (espejo de MAPA B que escribe el LOGO! con *Network Output*) y **coils
  1000..1031** (comandos de la nube; el LOGO! los lee con *Network Input*; los
  pulsos se auto-limpian tras 1,5 s).
- **`mqtt_bridge`** (`PubSubClient` + `ArduinoJson`, TLS opcional): publica
  `gw/state`, `nodes`, `plant`, `station/<s>/data` y `node/<addr>/raw` en
  `aysafi/<sitio>/orq/…`; se suscribe a `station/+/cmd`, `node/+/relay`,
  `gw/cmd`, valida (resets con `arm`, rangos, anti-rebote) y responde `…/ack`.
- **SNTP** (`net_master`) para el `ts` de los payloads.
- Config completa en el **portal cautivo** (fieldset "Puente MQTT": habilitar,
  broker, puerto, TLS, usuario/clave, sitio, periodo).
- `scale/set` desde MQTT: reservado, aún no implementado (responde `ack ok:false`).
- La reconexión MQTT bloquea ~4 s; reintenta cada 20 s. Habilítalo solo con un
  broker alcanzable.

## 1.2.0 — OTA vía GitHub Releases

- **OTA "GitHub Releases pull"** (`src/ota_update.{h,cpp}`, módulo común de
  `ORCHESTRATION/tools/ota/`): con la WiFi STA arriba, el gateway consulta
  `releases/latest/download/version.txt`; si hay versión nueva descarga
  `firmware.bin`, verifica el **SHA-256** contra `firmware.sha256` mientras
  escribe la partición OTA libre, y reinicia. Progreso en el OLED.
- Chequeo **al conectar la WiFi** y luego **cada 6 h** (solo en `MODE_NORMAL`).
  Durante la descarga (~30–60 s) el servidor Modbus queda en pausa.
- `.github/workflows/release.yml`: un tag `vX.Y.Z` sobre `main` compila con
  PlatformIO y publica el Release `latest` con los 3 assets. No requiere secrets
  (la WiFi del gateway vive en NVS / portal cautivo).
- `platformio.ini`: `build_flags = ${sysenv.EXTRA_BUILD_FLAGS}` para recibir
  `FW_VERSION_OVERRIDE` del CI.
- `FW_VERSION` (`V1.2026.006-gw`) se mantiene como cadena descriptiva del OLED;
  el canal OTA usa `FW_SEMVER` (`1.2.0`).
- **Disparar OTA de un nodo:** `masterOtaTrigger(slot)` (`lora_master.{h,cpp}`)
  envía `OTA,<mac>` por LoRa y espera el `ACK`. Botón **OTA** por fila de nodo en
  el portal cautivo (`/nodeota`). El nodo (nodeIO ≥ 1.3.0) reinicia en modo
  actualización y baja el firmware por su WiFi de mantenimiento.

> **A partir de este flasheo el gateway se actualiza solo.** El último flasheo
> por USB debe llevar esta versión (o posterior).
