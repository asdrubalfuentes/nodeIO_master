# Flasheo por USB — `nodeIO_master`

Placa: **Heltec WiFi LoRa 32 V3** (ESP32-S3, flash 8 MB, QIO @ 80 MHz).
Tabla de particiones `default_8MB.csv` (**dual-OTA**: app0/app1 de 3.19 MB).

> El flasheo por USB solo hace falta para el **primer arranque** de un equipo o
> tras subir `CFG_MAGIC`. Después el gateway se autoactualiza por OTA (ver
> `README.md` §9 y `../ORCHESTRATION/OTA_ROLLOUT.md`).

## Desde el repo

```sh
pio run -t upload
```

## Con esptool (binarios sueltos)

| Fichero | Offset |
|---|---|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

```sh
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z \
  0x0000 bootloader.bin  0x8000 partitions.bin  0xe000 boot_app0.bin  0x10000 firmware.bin

# solo la app (bootloader ya presente):
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z 0x10000 firmware.bin
```

## Notas

- **Transporte Modbus por defecto: TCP :502** sobre WiFi STA (se configura la red
  de planta en el portal). RTU en RS-485/USB queda como respaldo de banco.
- Con `mbUsb = true` (RTU por USB, UART0 GPIO43/44) la consola de log se
  desactiva. Para RS-485 por `Serial1`, desmarca "Salir por USB" en el portal.
- El mapa Modbus (MAPA A) está en `README.md` §5 y en
  `../ORCHESTRATION/REGISTER_MAP.md` — está **congelado byte a byte**.

## Reset por DTR/RTS al abrir el puerto (CP210x)

El USB Heltec resetea el ESP32 si DTR/RTS van altos al abrir el puerto → bootloop
cuando un cliente Modbus se conecta sin cuidado. En Python:

```python
ser = serial.Serial()
ser.port = "COM8"; ser.baudrate = 19200
ser.dtr = False; ser.rts = False        # ANTES de open()
ser.open(); time.sleep(3)               # espera reset + LoRa acquire
```

Los scripts de `test/tools/` ya lo hacen. Alternativa hardware: cable USB sin
DTR/RTS, o puentear el cap de reset (C9).

## Pines (configurables en el portal)

| Señal | Pin por defecto |
|---|---|
| Modbus RX / TX | GPIO43 / GPIO44 (UART0/USB) · o GPIO2 / GPIO3 (Serial1) |
| Modbus DE/RE | GPIO4 (solo Serial1/RS-485) |
| Botón F1 / F2 | GPIO47 / GPIO48 |
| Botón Builtin (portal, >5 s) | GPIO0 |
