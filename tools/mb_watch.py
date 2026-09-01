#!/usr/bin/env python3
"""Poll the gateway on an interval and reprint the snapshot.

    python tools/mb_watch.py --port COM8 --interval 1
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime

from mb_lib import Gateway, add_serial_args, open_client


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    add_serial_args(ap)
    ap.add_argument("--interval", type=float, default=1.0, help="seconds between polls")
    ap.add_argument("--no-clear", action="store_true", help="do not clear the screen")
    a = ap.parse_args()

    c = open_client(a)
    gw = Gateway(c, a.slave)
    errs = 0
    try:
        while True:
            try:
                g = gw.globals()
                nodes = gw.nodes(with_bits=False)
                errs = 0
                if not a.no_clear:
                    os.system("cls" if os.name == "nt" else "clear")
                print(f"{datetime.now():%H:%M:%S}  {a.port} {a.baud} 8{a.parity}{a.stopbits}"
                      f"  slave {a.slave}   nodes {g.online}/{g.node_count}"
                      f"  marker 0x{g.marker:04X}")
                print("slot addr link  AI1  AI2  AI3  AI4   DI    RO  rssi  age")
                for n in nodes:
                    if not n.present:
                        continue
                    di = "".join(map(str, n.di))
                    ro = "".join(map(str, n.relay_on))
                    print(f"{n.slot:>4} {n.addr:>4} {'UP ' if n.online else 'dn '}  "
                          f"{n.ai[0]:>4} {n.ai[1]:>4} {n.ai[2]:>4} {n.ai[3]:>4}  "
                          f"{di}  {ro}  {n.rssi:>4} {n.age_s:>4}")
            except IOError as e:
                errs += 1
                print(f"[{datetime.now():%H:%M:%S}] modbus error ({errs}): {e}",
                      file=sys.stderr)
            time.sleep(a.interval)
    except KeyboardInterrupt:
        print("\nbye")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
