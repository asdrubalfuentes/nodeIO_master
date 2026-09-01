# Binarios precompilados — `V1.2026.005-gw`

Placa: **Heltec WiFi LoRa 32 V3** (ESP32-S3, flash 8 MB, modo QIO @ 80 MHz).

| Fichero | Offset |
|---|---|
| `bootloader.bin` | `0x0000` |
| `partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `firmware.bin` | `0x10000` |

## Flashear con esptool

```sh
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 firmware.bin
```

Solo la app (si el bootloader ya está):

```sh
esptool.py --chip esp32s3 --port COM? --baud 460800 write_flash -z 0x10000 firmware.bin
```

O desde el repo: `pio run -t upload`.

## Notas

- Modbus RTU sale por defecto por **USB (UART0, GPIO43/44)** con `mbUsb = true`;
  la consola de log queda desactivada en ese modo. Para RS-485 por `Serial1`,
  desmarca "Salir por USB" en el portal.
- `V1.2026.005-gw`: los coils de disparo de pulso (`b+4..7`) ya aceptan FC05
  (write single coil) sin devolver excepción; se autolimpian en el siguiente
  ciclo de publicación (~100 ms). Sin cambio de `CFG_MAGIC` respecto a `.004`.
- Actualizar desde `V1.2026.003-gw` o anterior **reinicia la tabla de nodos**
  (cambió `CFG_MAGIC` al añadir `mbUsb`).
