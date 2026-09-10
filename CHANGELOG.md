# Changelog — nodeIO_master

Formato de versión del canal OTA: `MAJOR.MINOR.PATCH` (semver numérico).
El firmware embebe `FW_SEMVER`; el CI lo sobreescribe desde el tag `vX.Y.Z`.

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
