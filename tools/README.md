# tools/ — pruebas Modbus RTU

Scripts Python para ejercitar el esclavo Modbus del gateway `nodeIO_master`.
Por defecto asumen el firmware con `mbUsb = true`: Modbus sale por el **USB
(UART0, GPIO43/44)**, esclavo **1**, **19200 8E1**.

## Instalación

```sh
pip install -r tools/requirements.txt
```

## Requisitos en el dispositivo

El esclavo Modbus solo corre en `MODE_NORMAL`, que necesita **≥1 nodo adoptado**.
Con la tabla vacía el firmware arranca en el portal y no responde Modbus.

## Uso

```sh
# snapshot único (bloque global + 8 bloques de nodo, coils y discrete inputs)
python tools/mb_dump.py --port COM8
python tools/mb_dump.py --port COM8 --raw          # + registros crudos
python tools/mb_dump.py --port COM8 --parity N     # si cambiaste el formato a 8N1

# monitor continuo
python tools/mb_watch.py --port COM8 --interval 1

# escribir un relé de un nodo (slot 0..7)
python tools/mb_relay.py --port COM8 --node 0 --relay 1 --on
python tools/mb_relay.py --port COM8 --node 0 --relay 1 --off
python tools/mb_relay.py --port COM8 --node 0 --relay 2 --pulse
```

Flags comunes: `--port --baud --parity {N,E,O} --stopbits {1,2} --slave --timeout`.

## Mapa de registros

En `mb_lib.py` (docstring) y en `../user manual.md` §4. Resumen: por nodo `i`,
base `i*16`; Input Registers FC04 con AI/DI/relés/link/RSSI/edad/addr; Discrete
Inputs FC02 con DI + link; Coils FC01/05 con consigna (`b+0..3`) y pulso
(`b+4..7`). Bloque global en Ireg 900.
