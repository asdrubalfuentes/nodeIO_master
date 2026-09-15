# Changelog — nodeIO_master

Formato de versión del canal OTA: `MAJOR.MINOR.PATCH` (semver numérico).
El firmware embebe `FW_SEMVER`; el CI lo sobreescribe desde el tag `vX.Y.Z`.

## 1.5.4 — diagnóstico: monitoreo de heap (investigando OLED "enbasurado")

- **Reporte de campo:** el OLED muestra un patrón de ruido tipo "matrix
  horizontal", se limpia solo con el reinicio (incluso varias veces), y
  **no le pasa a `nodeIO`** (mismo hardware/librería de OLED, mucho menos
  tráfico de heap). El framebuffer de la librería SSD1306 se reserva con
  `malloc()` en el heap (`OLEDDisplay.cpp`) — un candidato natural es
  fragmentación/corrupción de heap por el tráfico continuo de otros
  módulos (candidatos: `JsonDocument` del puente MQTT en cada publicación,
  TLS del chequeo OTA), algo que `nodeIO` casi no genera.
- Este release **no corrige nada todavía** — solo agrega `[heap] libre=...
  minimo_desde_boot=... bloque_max=...` cada 5 min por Serial, para
  correlacionar la caída de heap con el momento real en que aparece el
  patrón la próxima vez. Pendiente: revisar con datos reales una vez que
  el equipo lo reciba por OTA y se repita el problema.

## 1.5.3 — fix: sin DNS con IP fija (OTA y SNTP rotos por igual)

- `WiFi.config()` en modo IP fija (`mcfg.staStatic`) no pasaba `dns1`/`dns2` -- a
  diferencia de DHCP (que recibe el DNS del router automaticamente), el
  gateway quedaba **sin ningun servidor DNS configurado** y `hostByName()`
  fallaba siempre, el 100% de las veces, sin importar la red ni cuanto
  tiempo pasara desde el arranque. Afectaba por igual al chequeo OTA
  (`github.com`) y a la hora SNTP (`pool.ntp.org`/`time.google.com`) que usa
  el cierre automatico de dia/mes -- ambos quedaban rotos en cualquier
  gateway configurado con IP fija.
- Fix: `dns1` = el propio gateway LAN (la mayoria de los routers hacen de
  proxy DNS), `dns2` = `8.8.8.8` de respaldo.

## 1.5.2 — comando serial "buscar actualizacion"

- Alternativa de banco al F2 mantenido 4-5s: escribir `buscar actualizacion`
  (o `ota`) por Serial/USB dispara el mismo chequeo OTA forzado. Util con el
  equipo conectado solo por USB o para automatizarlo desde un script. Se
  desactiva solo si el USB esta en uso como transporte Modbus RTU (`mbUsb`).

## 1.5.1 — fix: nodo adoptado siempre offline (timeout ACK corto para la trama v3)

- **`ackTimeoutMs` por defecto `500`→`2000` ms.** La trama `ST` v3 (escalado +
  4 acumulados int32 + `almBits`, ~110-130 bytes) tarda ~650-700 ms de aire a
  SF9/BW125 — con 500 ms el gateway declaraba timeout **antes** de que la
  respuesta del nodo terminara de llegar. Síntoma en campo: nodo adoptado
  (ADOPT usa una trama `ACK` corta que sí alcanzaba a tiempo), pero
  `RD` de sondeo normal fallaba el 100% de las veces → tabla del gateway
  mostraba el nodo permanentemente `offline`/`edad=65535`, mientras el propio
  nodo mostraba `rx==tx` (sí contestaba). Diagnosticado con logs de depuración
  temporales en `lora_master.cpp` (revertidos) + captura serial en vivo.
  Equipos ya en campo: subir "Timeout ACK ms" a 2000 en el portal LoRa, no
  hace falta reflashear — el default de fábrica solo aplica a NVS nueva/borrada.
- **Fix pantalla OTA (OLED) "parpadea y no muestra nada":** tras un chequeo
  OTA manual (F2 4-5s) que no reinicia (al día / error), el `loop()` normal
  repintaba la pantalla de estado por encima en <250 ms — el resultado de
  `ota::run()` apenas alcanzaba a parpadear. `otaMaybeCheck(true)` ahora
  sostiene el resultado en pantalla ~1.8 s antes de volver al flujo normal.

## 1.5.0 — MAPA A2 + cierre automático de día/mes + F2 OTA (cambio de rumbo)

- **MAPA A2** (`modbus_gw`): nuevo bloque Input Registers (base `200+i*16`) con
  nivel/caudal escalados, los 4 acumulados (día/mes × nivel/caudal, m³ ×1000
  int32 hi-first) y `almBits` por nodo — lo que el nodo ya calcula (ver
  `nodeIO` 1.4.0), el gateway solo lo relee y republica.
- `lora_master`: `NodeSnapshot` extendido; `parseStatus()` lee el `ST`
  extendido si el nodo lo trae (compatible con nodos en firmware viejo —
  strtok_r da NULL y se queda con el último valor). `masterQueueCloseDay()`/
  `CloseMonth()` (comandos `CD`/`CM`).
- **Cierre automático de día/mes**: el gateway (con hora real vía SNTP) revisa
  1x/min y dispara `CD`/`CM` a todos los nodos adoptados en el cruce de
  día/mes, en hora local (`tz` configurable en el portal, default Chile
  continental). El nodo nunca cierra solo.
- **F2 mantenido 4-5s** (modo normal): fuerza el chequeo OTA del propio
  gateway ya, sin esperar la ventana de 6h.

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
