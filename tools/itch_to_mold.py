#!/usr/bin/env python3
"""
itch_to_mold.py -- Nasdaq ITCH 5.0 BinaryFILE -> MoldUDP64 packet stream.

Nasdaq's sample/historical files (e.g. 20190730.BX_ITCH_50[.gz]) are
"BinaryFILE": every message is a 2-byte big-endian length followed by the
message. That record is byte-for-byte a MoldUDP64 message block, so framing
is just grouping consecutive records into packets and putting a 20-byte
MoldUDP64 header in front of each.

The output (.mold) is the packets back to back. Each packet is
self-delimiting (header message count + per-message lengths), so the same
file feeds both the golden model (Orderbook-cpp reads it directly) and a
replay sender (each packet becomes one UDP datagram).

Usage:
    tools/itch_to_mold.py IN [OUT] [--symbol SPY] [--book-only]
                          [--max-payload 1400] [--limit N]
                          [--session NAME] [--start-seq 1]

Filters:
    --symbol SYM   keep only messages for SYM's stock locate (found from the
                   'R' stock-directory messages) plus system-wide messages
                   (locate 0, e.g. 'S' system events).
    --book-only    keep only the types the book consumes: A F E C X D U.

  Filtered streams are for golden-model runs. For RTL stimulus, frame the
  file UNFILTERED: a live feed carries every symbol and type, and the RTL's
  skip path (e.g. 44-byte 'P' trades, longer than any book message) is
  exactly what needs testing.

Sequence numbers count the messages actually written, starting at
--start-seq, so a filtered stream has no gaps.
"""

from __future__ import annotations

import argparse
import gzip
import struct
import sys
from pathlib import Path

MOLD_HEADER = struct.Struct(">10sQH")   # session, sequence number, message count
MOLD_HEADER_LEN = MOLD_HEADER.size      # 20
MAX_MESSAGES_PER_PACKET = 0xFFFE        # 0xFFFF means end-of-session
BOOK_TYPES = frozenset(b"AFECXDU")
STOCK_DIRECTORY = ord("R")


def read_records(stream):
    """Yields (length, message bytes) from a BinaryFILE stream."""
    read = stream.read
    while True:
        prefix = read(2)
        if len(prefix) < 2:
            if prefix:
                raise ValueError("truncated length prefix at end of file")
            return
        (length,) = struct.unpack(">H", prefix)
        msg = read(length)
        if len(msg) != length:
            raise ValueError(f"truncated message: wanted {length} bytes, got {len(msg)}")
        yield length, msg


class Framer:
    """Packs messages greedily into packets of at most max_payload bytes."""

    def __init__(self, out, session: bytes, start_seq: int, max_payload: int):
        self.out = out
        self.session = session
        self.next_seq = start_seq
        self.max_payload = max_payload
        self.blocks: list[bytes] = []
        self.size = MOLD_HEADER_LEN
        self.packets = 0
        self.messages = 0

    def add(self, msg: bytes) -> None:
        block = struct.pack(">H", len(msg)) + msg
        if self.blocks and (self.size + len(block) > self.max_payload
                            or len(self.blocks) == MAX_MESSAGES_PER_PACKET):
            self.flush()
        self.blocks.append(block)
        self.size += len(block)

    def flush(self) -> None:
        if not self.blocks:
            return
        count = len(self.blocks)
        self.out.write(MOLD_HEADER.pack(self.session, self.next_seq, count))
        self.out.write(b"".join(self.blocks))
        self.next_seq += count
        self.packets += 1
        self.messages += count
        self.blocks = []
        self.size = MOLD_HEADER_LEN


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("input", type=Path, help="ITCH 5.0 BinaryFILE (.gz is read directly)")
    ap.add_argument("output", type=Path, nargs="?", help="default: input name + .mold")
    ap.add_argument("--symbol", help="keep only this symbol's messages (+ system-wide ones)")
    ap.add_argument("--book-only", action="store_true", help="keep only A F E C X D U")
    ap.add_argument("--max-payload", type=int, default=1400,
                    help="max UDP payload per packet (default 1400, under a 1500 MTU)")
    ap.add_argument("--limit", type=int, help="stop after writing this many messages")
    ap.add_argument("--session", default="BINFILE", help="MoldUDP64 session (10 chars max)")
    ap.add_argument("--start-seq", type=int, default=1)
    args = ap.parse_args()

    if args.max_payload < MOLD_HEADER_LEN + 2 + 50:
        ap.error("--max-payload too small to hold one ITCH message")
    session = args.session.encode("ascii")
    if len(session) > 10:
        ap.error("--session is at most 10 characters")
    symbol = args.symbol.encode("ascii").ljust(8) if args.symbol else None

    out_path = args.output or args.input.with_name(
        args.input.name.removesuffix(".gz") + (f".{args.symbol}" if args.symbol else "") + ".mold")
    opener = gzip.open if args.input.suffix == ".gz" else open

    locate = None
    read_count = 0
    with opener(args.input, "rb") as src, open(out_path, "wb") as dst:
        framer = Framer(dst, session.ljust(10), args.start_seq, args.max_payload)
        for _, msg in read_records(src):
            read_count += 1
            mtype = msg[0]
            if symbol is not None:
                msg_locate = int.from_bytes(msg[1:3], "big")
                if mtype == STOCK_DIRECTORY and msg[11:19] == symbol:
                    locate = msg_locate
                if msg_locate != 0 and msg_locate != locate:
                    continue
            if args.book_only and mtype not in BOOK_TYPES:
                continue
            framer.add(msg)
            if args.limit and framer.messages + len(framer.blocks) >= args.limit:
                break
        framer.flush()

    if symbol is not None and locate is None:
        out_path.unlink()   # don't leave an empty stream that looks already framed
        print(f"symbol {args.symbol!r} not found in any 'R' stock-directory message",
              file=sys.stderr)
        return 1

    print(f"read {read_count:,} messages, wrote {framer.messages:,} in "
          f"{framer.packets:,} packets to {out_path}"
          + (f"  ({args.symbol} = locate {locate})" if symbol is not None else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
