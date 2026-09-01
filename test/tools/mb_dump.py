#!/usr/bin/env python3
"""One-shot snapshot of the nodeIO_master Modbus gateway.

    python tools/mb_dump.py --port COM8
    python tools/mb_dump.py --port COM8 --parity N        # if mbFormat = 8N1
    python tools/mb_dump.py --port COM8 --raw             # also print raw registers
"""
from __future__ import annotations

import argparse
import sys
from datetime import datetime

import mb_lib
from mb_lib import Gateway, add_serial_args, open_client


def fmt_bits(bits: list[int]) -> str:
    return "".join(str(b) for b in bits) if bits else "----"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    add_serial_args(ap)
    ap.add_argument("--raw", action="store_true", help="also dump raw input registers")
    a = ap.parse_args()

    c = open_client(a)
    gw = Gateway(c, a.slave)
    try:
        print(f"# {datetime.now():%Y-%m-%d %H:%M:%S}  {a.port} "
              f"{a.baud} 8{a.parity}{a.stopbits}  slave {a.slave}")

        g = gw.globals()
        print("\n== GLOBAL (Ireg 900) ==")
        print(f"  marker      0x{g.marker:04X}  ({'OK' if g.marker_ok else 'UNEXPECTED, wrong slave/params?'})")
        print(f"  nodeCount   {g.node_count}")
        print(f"  online      {g.online}")
        print(f"  local IO    {'on' if g.local_io else 'off'}")
        if g.local_io:
            print(f"  local AI    {g.local_ai}")
            print(f"  local DI    {fmt_bits(g.local_di)}")
            print(f"  local RO    {fmt_bits(g.local_relay)}")

        print("\n== NODES (Ireg i*16) ==")
        hdr = ("slot addr link  AI1  AI2  AI3  AI4  DI    RO   ROdis "
               "rssi  age  coilSP coilPls")
        print(hdr)
        print("-" * len(hdr))
        any_node = False
        for n in gw.nodes(with_bits=True):
            if not n.present:
                continue
            any_node = True
            print(f"{n.slot:>4} {n.addr:>4} {'UP  ' if n.online else 'down'} "
                  f"{n.ai[0]:>4} {n.ai[1]:>4} {n.ai[2]:>4} {n.ai[3]:>4} "
                  f"{fmt_bits(n.di)} {fmt_bits(n.relay_on)} {fmt_bits(n.relay_dis)}  "
                  f"{n.rssi:>4} {n.age_s:>4}  {fmt_bits(n.coil_setpoint)}   {fmt_bits(n.coil_pulse)}")
        if not any_node:
            print("  (no populated slots -- adopt a node from the portal first)")

        if a.raw:
            print("\n== RAW input registers ==")
            for i in range(mb_lib.MAX_NODES):
                r = gw._iregs(i * mb_lib.NODE_BLK, mb_lib.NODE_BLK)
                print(f"  blk {i} @ {i*mb_lib.NODE_BLK:>3}: {r}")
            print(f"  global  @ 900: {gw._iregs(mb_lib.GLOBAL_BASE, 10)}")
        return 0
    except IOError as e:
        print(f"\nMODBUS ERROR: {e}", file=sys.stderr)
        print("hints: device must be in MODE_NORMAL (>=1 adopted node); "
              "check --parity (firmware default 8E1) and --slave.", file=sys.stderr)
        return 1
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
