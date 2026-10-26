#!/usr/bin/env python3
"""
ob -- one command for the everyday golden-model workflow, by symbol.

    ob run SPY                 build, frame the feed for SPY (first time), run the model
    ob book SPY [--at K] [--depth N] [--orders]
                               the order book after message K (default: end of day)
    ob bbo SPY [--start K] [--count N]
                               best bid/offer changes, from the snapshots
    ob symbols [--top N]       symbols in the feed, busiest first
    ob test                    build and run all tests

Files live in sample/, named by symbol:
    sample/SPY.mold            the symbol's MoldUDP64 packets (from the feed)
    sample/SPY.snapshot.bin    the model's BBO snapshots
    sample/SPY.trace.txt       the model's per-message trace (what `book` reads)
    sample/SPY.state.txt       end-of-run book dump

The feed is the first Nasdaq ITCH file in sample/ (*_ITCH_50[.gz]); pass
--feed to pick another. The build directory is cmake-build-debug/ if it
exists, else build/ (configured on first use); OB_BUILD overrides.

Tip -- add to ~/.bashrc so it is just `ob`:
    alias ob="python3 /data/projects/Orderbook-cpp/tools/ob.py"
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
SAMPLE = ROOT / "sample"


def build_dir() -> Path:
    if os.environ.get("OB_BUILD"):
        return Path(os.environ["OB_BUILD"]).resolve()
    debug = ROOT / "cmake-build-debug"
    return debug if debug.is_dir() else ROOT / "build"


def run(cmd: list, **kw) -> None:
    print("$ " + " ".join(str(c) for c in cmd), flush=True)
    result = subprocess.run([str(c) for c in cmd], **kw)
    if result.returncode != 0:
        sys.exit(result.returncode)


def build() -> Path:
    b = build_dir()
    if not (b / "CMakeCache.txt").exists():
        run(["cmake", "-S", ROOT, "-B", b, "-DCMAKE_BUILD_TYPE=Release"], stdout=subprocess.DEVNULL)
    run(["cmake", "--build", b, "-j"], stdout=subprocess.DEVNULL)
    exe = b / ("Orderbook-cpp.exe" if os.name == "nt" else "Orderbook-cpp")
    if not exe.exists():
        sys.exit(f"build finished but {exe} is missing")
    return exe


def find_feed(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit).resolve()
    candidates = sorted(SAMPLE.glob("*_ITCH_50")) + sorted(SAMPLE.glob("*_ITCH_50.gz"))
    if not candidates:
        sys.exit(f"no Nasdaq ITCH file (*_ITCH_50 or *_ITCH_50.gz) in {SAMPLE}; pass --feed")
    return candidates[0]


def files(sym: str) -> dict[str, Path]:
    return {k: SAMPLE / f"{sym}.{k}" for k in ("mold", "snapshot.bin", "trace.txt", "state.txt")}


def need(path: Path, sym: str) -> Path:
    if not path.exists():
        sys.exit(f"{path.name} not found -- run `ob run {sym}` first")
    return path


def cmd_run(a) -> None:
    sym = a.symbol.upper()
    f = files(sym)
    exe = build()
    feed = find_feed(a.feed)
    if not f["mold"].exists() or f["mold"].stat().st_mtime < feed.stat().st_mtime:
        run([sys.executable, "-I", TOOLS / "itch_to_mold.py", feed, f["mold"], "--symbol", sym])
    cmd = [exe, f["mold"], f["snapshot.bin"], "--symbol", sym,
           "--trace", f["trace.txt"], "--state", f["state.txt"]]
    if a.base_price is not None:
        cmd += ["--base-price", a.base_price]
    run(cmd)
    print(f"\nnext:  ob book {sym} --at <msg>     ob bbo {sym}")


def cmd_book(a) -> None:
    sym = a.symbol.upper()
    cmd = [sys.executable, "-I", TOOLS / "show_book.py", "book", need(files(sym)["trace.txt"], sym),
           "--depth", a.depth]
    if a.at is not None:
        cmd += ["--at", a.at]
    if a.orders:
        cmd.append("--orders")
    run(cmd)


def cmd_bbo(a) -> None:
    sym = a.symbol.upper()
    run([sys.executable, "-I", TOOLS / "show_book.py", "snapshot",
         need(files(sym)["snapshot.bin"], sym), "--start", a.start, "--count", a.count, "--changes"])


def cmd_symbols(a) -> None:
    feed = find_feed(a.feed)
    print(f"scanning {feed.name} (about half a minute for a full day)...", flush=True)
    import gzip
    opener = gzip.open if feed.suffix == ".gz" else open
    names, counts = {}, Counter()
    with opener(feed, "rb") as f:
        read = f.read
        while True:
            prefix = read(2)
            if len(prefix) < 2:
                break
            msg = read(struct.unpack(">H", prefix)[0])
            t = msg[0]
            if t in b"AFECXDU":
                counts[msg[1] << 8 | msg[2]] += 1
            elif t == 0x52:
                names[msg[1] << 8 | msg[2]] = msg[11:19].decode().strip()
    print(f"\n{len(counts):,} symbols with book messages; busiest {a.top}:\n")
    for loc, c in counts.most_common(a.top):
        print(f"  {names.get(loc, '?'):8} {c:>10,} book messages")


def cmd_test(_a) -> None:
    build()
    run(["ctest", "--test-dir", build_dir(), "-j8", "--output-on-failure"])


def main() -> None:
    ap = argparse.ArgumentParser(prog="ob", description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True, metavar="{run,book,bbo,symbols,test}")

    p = sub.add_parser("run", help="build, frame and run one symbol")
    p.add_argument("symbol")
    p.add_argument("--feed", help="Nasdaq ITCH BinaryFILE (default: first in sample/)")
    p.add_argument("--base-price", type=int, help="price-window base (default: first add)")
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("book", help="order book ladder after a message")
    p.add_argument("symbol")
    p.add_argument("--at", type=int, help="message number (default: end of day)")
    p.add_argument("--depth", type=int, default=10, help="levels per side (default 10)")
    p.add_argument("--orders", action="store_true", help="list each level's orders")
    p.set_defaults(fn=cmd_book)

    p = sub.add_parser("bbo", help="best bid/offer changes")
    p.add_argument("symbol")
    p.add_argument("--start", type=int, default=0, help="first snapshot to show")
    p.add_argument("--count", type=int, default=30, help="lines to show (0 = all)")
    p.set_defaults(fn=cmd_bbo)

    p = sub.add_parser("symbols", help="symbols in the feed, busiest first")
    p.add_argument("--feed")
    p.add_argument("--top", type=int, default=20)
    p.set_defaults(fn=cmd_symbols)

    p = sub.add_parser("test", help="build and run all tests")
    p.set_defaults(fn=cmd_test)

    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
