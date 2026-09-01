"""Shared helpers to talk to the nodeIO_master Modbus RTU gateway.

Register map (see src/modbus_gw.h and "user manual.md" section 4):

  Per node slot i = 0..7, base b = i*16
    Input Register (FC04)
      b+0..3  AI1..AI4                (0..4095)
      b+4     DI bitfield             (bit0..3 = DI1..DI4)
      b+5     relay bitfield          (bit0..3 = closed, bit8..11 = disabled 'x')
      b+6     link                    (0 = offline, 1 = online)
      b+7     RSSI dBm                (int16, signed)
      b+8     seconds since last reply
      b+9     assigned LoRa address   (0 = empty slot)
    Discrete Input (FC02)  b+0..3 DI1..DI4 ; b+4 link online
    Coil (FC01/05/15)      b+0..3 relay setpoint RO1..RO4 (write 1 = close)
                           b+4..7 pulse trigger  RO1..RO4 (write 1 -> pulse, auto-clears)

  Global Input Register
    900  proto marker 0x0203
    901  configured node count
    902  online node count
    903  local IO enabled (0/1)
    904..907  local AI1..AI4   (only if local IO enabled)
    908  local DI bits         (only if local IO enabled)
    909  local relay bits      (only if local IO enabled)
  Global Coil
    900..903  local relays     (only if local IO enabled)
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field

import serial as pyserial
from pymodbus.client import ModbusSerialClient

MAX_NODES = 8
NODE_BLK = 16
GLOBAL_BASE = 900
PROTO_MARKER = 0x0203


def s16(v: int) -> int:
    """Interpret a 16-bit register as a signed int16."""
    return v - 0x10000 if v & 0x8000 else v


def bits4(v: int) -> list[int]:
    return [(v >> k) & 1 for k in range(4)]


# --------------------------------------------------------------------------- CLI
def add_serial_args(ap: argparse.ArgumentParser) -> None:
    ap.add_argument("--port", default="COM8", help="serial port (default COM8)")
    ap.add_argument("--baud", type=int, default=19200, help="baud (default 19200)")
    ap.add_argument("--parity", default="E", choices=["N", "E", "O"],
                    help="parity, must match mbFormat (default E = 8E1)")
    ap.add_argument("--stopbits", type=int, default=1, choices=[1, 2])
    ap.add_argument("--slave", type=int, default=1, help="Modbus slave id (default 1)")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds to wait after opening the port before talking "
                         "Modbus, so a reset + LoRa re-acquire can finish "
                         "(default 3.0)")


def client_from_args(a: argparse.Namespace) -> ModbusSerialClient:
    return ModbusSerialClient(
        port=a.port,
        baudrate=a.baud,
        bytesize=8,
        parity=a.parity,
        stopbits=a.stopbits,
        timeout=a.timeout,
    )


def open_client(a: argparse.Namespace) -> ModbusSerialClient:
    """Open the port with DTR/RTS held low so the CP210x auto-reset circuit
    doesn't reboot the ESP32 on connect, hand the serial object to pymodbus,
    then wait out any reset that still slipped through."""
    c = client_from_args(a)
    ser = pyserial.Serial()
    ser.port = a.port
    ser.baudrate = a.baud
    ser.bytesize = 8
    ser.parity = a.parity
    ser.stopbits = a.stopbits
    ser.timeout = a.timeout
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except Exception as e:
        print(f"ERROR: cannot open {a.port}: {e}", file=sys.stderr)
        raise SystemExit(2)
    for _ in range(2):
        try:
            ser.dtr = False
            ser.rts = False
        except Exception:
            pass
    c.socket = ser  # pymodbus.connect() is now a no-op; it uses this socket

    settle = getattr(a, "settle", 3.0)
    if settle > 0:
        time.sleep(settle)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    return c


# ----------------------------------------------------------------------- decode
@dataclass
class NodeView:
    slot: int
    addr: int
    online: bool
    ai: list[int]
    di: list[int]
    relay_on: list[int]
    relay_dis: list[int]
    rssi: int
    age_s: int
    di_discrete: list[int] = field(default_factory=list)
    coil_setpoint: list[int] = field(default_factory=list)
    coil_pulse: list[int] = field(default_factory=list)

    @property
    def present(self) -> bool:
        return self.addr != 0


@dataclass
class GlobalView:
    marker: int
    node_count: int
    online: int
    local_io: bool
    local_ai: list[int]
    local_di: list[int]
    local_relay: list[int]

    @property
    def marker_ok(self) -> bool:
        return self.marker == PROTO_MARKER


class Gateway:
    def __init__(self, client: ModbusSerialClient, slave: int = 1, retries: int = 3):
        self.c = client
        self.slave = slave
        self.retries = retries

    def _do(self, label: str, fn):
        last = None
        for attempt in range(self.retries):
            try:
                rr = fn()
                if not rr.isError():
                    return rr
                last = rr
            except Exception as e:  # transport hiccup during a board reset
                last = e
            time.sleep(0.3)
        raise IOError(f"{label} -> {last}")

    # -- raw reads -------------------------------------------------------
    def _iregs(self, addr: int, count: int) -> list[int]:
        rr = self._do(f"read_input_registers({addr},{count})",
                      lambda: self.c.read_input_registers(addr, count, slave=self.slave))
        return list(rr.registers)

    def _discretes(self, addr: int, count: int) -> list[int]:
        rr = self._do(f"read_discrete_inputs({addr},{count})",
                      lambda: self.c.read_discrete_inputs(addr, count, slave=self.slave))
        return [int(b) for b in rr.bits[:count]]

    def _coils(self, addr: int, count: int) -> list[int]:
        rr = self._do(f"read_coils({addr},{count})",
                      lambda: self.c.read_coils(addr, count, slave=self.slave))
        return [int(b) for b in rr.bits[:count]]

    # -- writes -------------------------------------------------------
    def set_relay(self, slot: int, relay1: int, on: bool) -> None:
        addr = slot * NODE_BLK + (relay1 - 1)
        self._do(f"write_coil({addr},{on})",
                 lambda: self.c.write_coil(addr, bool(on), slave=self.slave))

    def pulse_relay(self, slot: int, relay1: int) -> None:
        addr = slot * NODE_BLK + 4 + (relay1 - 1)
        self._do(f"write_coil({addr},pulse)",
                 lambda: self.c.write_coil(addr, True, slave=self.slave))

    # -- decoded views -------------------------------------------------
    def node(self, slot: int, with_bits: bool = True) -> NodeView:
        b = slot * NODE_BLK
        r = self._iregs(b, 10)
        n = NodeView(
            slot=slot,
            addr=r[9],
            online=bool(r[6]),
            ai=r[0:4],
            di=bits4(r[4]),
            relay_on=bits4(r[5]),
            relay_dis=[(r[5] >> (8 + k)) & 1 for k in range(4)],
            rssi=s16(r[7]),
            age_s=r[8],
        )
        if with_bits and n.present:
            d = self._discretes(b, 5)
            n.di_discrete = d[0:4]
            co = self._coils(b, 8)
            n.coil_setpoint = co[0:4]
            n.coil_pulse = co[4:8]
        return n

    def nodes(self, with_bits: bool = True) -> list[NodeView]:
        return [self.node(i, with_bits) for i in range(MAX_NODES)]

    def globals(self) -> GlobalView:
        r = self._iregs(GLOBAL_BASE, 10)
        return GlobalView(
            marker=r[0],
            node_count=r[1],
            online=r[2],
            local_io=bool(r[3]),
            local_ai=r[4:8],
            local_di=bits4(r[8]),
            local_relay=bits4(r[9]),
        )
