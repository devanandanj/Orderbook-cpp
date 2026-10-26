#!/usr/bin/env python3
"""
show_book.py -- human-readable views of the golden model's outputs.

  show_book.py book TRACE [--at K] [--depth N] [--orders]
      The order book as a price ladder after message K (default: the last
      one), rebuilt by replaying trace.txt's memory writes. Asks above,
      bids below, best prices at the spread; per level: total shares and
      order count. --orders also lists each level's resting orders.

  show_book.py snapshot SNAPSHOT_BIN [--start K] [--count N] [--changes]
      Decodes snapshot.bin (v3: one 48-byte BBO packet per book message)
      into one line per packet. --changes prints only packets whose BBO
      differs from the previous one.

The ladder is rebuilt from the W,BID / W,ASK / W,TBL / W,CAM lines -- the
same writes the RTL is checked against -- and is cross-checked against the
trace's own BBO line at that message, so a replay error cannot pass
silently.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SNAP_LEN = 48
SNAP_FMT = struct.Struct(">HBBIQIIIIIIIHH")
STATUS_BITS = {0: "order_dropped", 1: "price_rejected", 2: "mold_gap_seen", 3: "unknown_order_ref"}


def dollars(price: int) -> str:
    return f"${price / 10_000:,.2f}"


# ---------------------------------------------------------------------------
# book: replay trace.txt
# ---------------------------------------------------------------------------

class Replay:
    def __init__(self) -> None:
        self.base = self.tick = self.center = None
        self.levels = {"BID": {}, "ASK": {}}    # slot -> (shares, orders)
        self.entries = {}                       # (TBL|CAM, addr) -> (orn, side, slot, shares)
        self.msg = None                         # last MSG line's fields
        self.bbo = None                         # last BBO line's fields
        self.applied = 0

    def price(self, slot: int) -> int:
        return self.base + (slot - self.center) * self.tick

    def feed(self, fields: list[str]) -> None:
        tag = fields[0]
        if tag == "W":
            target, index = fields[1], int(fields[2])
            if target in ("BID", "ASK"):
                shares, orders = int(fields[3]), int(fields[4])
                if orders:
                    self.levels[target][index] = (shares, orders)
                else:
                    self.levels[target].pop(index, None)
            else:
                key = (target, index)
                if fields[3] == "1":
                    self.entries[key] = (int(fields[4]), fields[5], int(fields[6]), int(fields[7]))
                else:
                    self.entries.pop(key, None)
        elif tag == "BBO":
            self.bbo = [int(x) for x in fields[1:7]]
        elif tag == "MSG":
            self.msg = fields
            self.applied += 1
        elif tag == "BOOK":
            self.base, self.tick, self.center = int(fields[1]), int(fields[2]), int(fields[3])

    def best(self, side: str):
        lv = self.levels[side]
        if not lv:
            return None
        return max(lv) if side == "BID" else min(lv)


def cmd_book(args) -> int:
    r = Replay()
    with open(args.trace, encoding="ascii") as f:
        pending: list[list[str]] = []   # one message's lines, applied at its END
        for line in f:
            line = line.strip()
            if not line:
                continue
            fields = line.split(",")
            if fields[0] == "BOOK":
                r.feed(fields)
                continue
            if fields[0] == "MSG" and args.at is not None and int(fields[1]) > args.at:
                break
            pending.append(fields)
            if fields[0] == "END":
                for p in pending:
                    r.feed(p)
                pending = []

    if r.base is None:
        print("trace has no BOOK header line (written by a newer Orderbook-cpp)", file=sys.stderr)
        return 1
    if r.msg is None:
        print("no messages applied at or before that index", file=sys.stderr)
        return 1

    # Cross-check against the model's own BBO for this message.
    bid, ask = r.best("BID"), r.best("ASK")
    rebuilt = [r.price(bid) if bid is not None else 0, *(r.levels["BID"][bid] if bid is not None else (0, 0)),
               r.price(ask) if ask is not None else 0, *(r.levels["ASK"][ask] if ask is not None else (0, 0))]
    if rebuilt != r.bbo:
        print(f"MISMATCH: ladder rebuilt from writes gives BBO {rebuilt}, trace says {r.bbo}",
              file=sys.stderr)
        return 1

    _, idx, mtype, orn, result, reason = (r.msg + [""] * 6)[:6]
    print(f"Book after message {idx} ({mtype} {orn} {result}{' ' + reason if reason else ''})"
          f" -- {r.applied:,} book messages applied, base {dollars(r.base)}")
    print(f"{len(r.levels['BID'])} bid levels, {len(r.levels['ASK'])} ask levels, "
          f"{len(r.entries)} resting orders (BBO cross-checked against trace)\n")

    by_level: dict[tuple[str, int], list[tuple[int, int]]] = {}
    if args.orders:
        for (orn, side, slot, shares) in r.entries.values():
            by_level.setdefault(("BID" if side == "B" else "ASK", slot), []).append((orn, shares))

    def row(side: str, slot: int, marker: str) -> None:
        shares, orders = r.levels[side][slot]
        print(f"  {side}  {dollars(r.price(slot)):>12}  {shares:>9,} sh  {orders:>4} ord{marker}")
        for orn, sh in sorted(by_level.get((side, slot), [])):
            print(f"                 order {orn:<20} {sh:>9,} sh")

    asks = sorted(r.levels["ASK"])[:args.depth]
    bids = sorted(r.levels["BID"], reverse=True)[:args.depth]
    for slot in reversed(asks):
        row("ASK", slot, "   <- best ask" if slot == ask else "")
    if bid is not None and ask is not None:
        spread = r.price(ask) - r.price(bid)
        print(f"  {'-' * 18} spread {spread // r.tick} ticks ({dollars(spread)}) {'-' * 18}")
    else:
        print(f"  {'-' * 18} {'no bids' if bid is None else 'no asks'} {'-' * 18}")
    for slot in bids:
        row("BID", slot, "   <- best bid" if slot == bid else "")
    if not asks and not bids:
        print("  (book empty)")
    return 0


# ---------------------------------------------------------------------------
# snapshot: decode snapshot.bin
# ---------------------------------------------------------------------------

def cmd_snapshot(args) -> int:
    data = Path(args.snapshot).read_bytes()
    if len(data) % SNAP_LEN:
        print(f"{args.snapshot}: {len(data)} bytes is not a whole number of {SNAP_LEN}-byte packets",
              file=sys.stderr)
        return 1
    total = len(data) // SNAP_LEN
    print(f"{total:,} packets\n")
    print(f"{'snap':>8} {'msg_index':>10}  {'bid':>12} {'shares':>8} {'n':>4} | "
          f"{'ask':<12} {'shares':>8} {'n':>4}  status")
    shown, prev = 0, None
    for k in range(args.start, total):
        (magic, version, status, snap_seq, _mold_seq, msg_index, _t_arr, _t_com,
         bp, bs, ap, as_, bn, an) = SNAP_FMT.unpack_from(data, k * SNAP_LEN)
        if magic != 0x4F42 or version != 3:
            print(f"packet {k}: magic {magic:#06x} version {version} -- not a v3 snapshot", file=sys.stderr)
            return 1
        bbo = (bp, bs, bn, ap, as_, an)
        if args.changes and bbo == prev and not status:
            continue
        prev = bbo
        flags = ",".join(name for bit, name in STATUS_BITS.items() if status >> bit & 1) or "-"
        bid = f"{dollars(bp):>12} {bs:>8,} {bn:>4}" if bp else f"{'-':>12} {'':>8} {'':>4}"
        ask = f"{dollars(ap):<12} {as_:>8,} {an:>4}" if ap else f"{'-':<12} {'':>8} {'':>4}"
        print(f"{snap_seq:>8} {msg_index:>10}  {bid} | {ask}  {flags}")
        shown += 1
        if args.count and shown >= args.count:
            break
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("book", help="price ladder after a message, from trace.txt")
    b.add_argument("trace", type=Path)
    b.add_argument("--at", type=int, help="msg_index to stop after (default: last)")
    b.add_argument("--depth", type=int, default=10, help="levels per side (default 10)")
    b.add_argument("--orders", action="store_true", help="list each level's resting orders")

    s = sub.add_parser("snapshot", help="decode snapshot.bin")
    s.add_argument("snapshot", type=Path)
    s.add_argument("--start", type=int, default=0, help="first packet to show")
    s.add_argument("--count", type=int, default=20, help="packets to show (0 = all; default 20)")
    s.add_argument("--changes", action="store_true", help="only packets where the BBO changed")

    args = ap.parse_args()
    return cmd_book(args) if args.cmd == "book" else cmd_snapshot(args)


if __name__ == "__main__":
    sys.exit(main())
