# Orderbook-cpp

A C++ order book that speaks ITCH 5.0 over MoldUDP64.

This is a golden model — a software reference for logic that is eventually meant to be implemented in RTL. The style reflects that constraint, not a design preference.

## Architecture

- **Flat structs, free functions** — no classes, no inheritance, no virtual dispatch.
- **No dynamic allocation** — fixed-size arrays, sized at compile time from `include/using.h`.
- **Mirrors the RTL's memories bit-exactly** — geometry is owned by `hardware-orderbook/spec/book.yaml`, and `spec/test_spec.py` checks the constants here against it.
  - **Order table** — order ref → {side, price slot, shares}. 1024 sets × 3 ways, set index = XOR-fold of the 64-bit ref, plus a 16-entry overflow CAM. An insert takes the lowest free way, then the lowest free CAM entry; if both are full the order is **dropped** (`order_dropped`).
  - **Price book** — per side, a {shares, orders} level for each of 16,384 price slots, $0.01 apart, centred on a per-session base price. An off-tick or out-of-window price is **rejected** (`price_rejected`). Best bid/ask is tracked incrementally and rescanned only when the best level empties.
- **ITCH 5.0** — A, F, E, C, X, D, U, decoded from raw big-endian bytes into one normalised `Command` (the parser → book-core boundary in the RTL too). `C` reduces the order's own level; its trade price is never used.
- **MoldUDP64 de-framing** — unpacks a packet into its constituent ITCH messages before dispatch.
- **Unknown refs are flagged, not fatal** — a D/U/E/C/X for an order the book dropped, rejected or already removed sets `unknown_order_ref` and leaves the book unchanged, matching the RTL.
- **Every memory write is logged** — each command records the table / CAM / level writes it made, in order. The testbench compares that log against the RTL's memory write ports; the wire carries only the BBO.
- **Warnings-as-errors** — every target builds with `/W4 /WX` (MSVC) or `-Wall -Wextra -Werror`, applied uniformly via a shared CMake helper.

## Components

| File | Purpose |
|---|---|
| `src/main.cpp` | Reads a file of back-to-back MoldUDP64 packets (sequence-gap checked), skips non-book types and other symbols, and per book message: parse → apply → trace → snapshot packet |
| `src/itchparser.cpp` | ITCH 5.0 message parsers (A, F, E, C, X, D, U) |
| `src/moldudp64.cpp` | MoldUDP64 packet de-framing |
| `src/command.cpp` | ITCH message → normalised `Command`; `Command` → book operation, status bits and trace reason |
| `src/book.cpp` | The book: add / delete / reduce / replace over the order table and price books, with the write log |
| `src/ordertable.cpp` | Hashed set-associative order table + overflow CAM: lookup and free-slot rules |
| `src/pricebook.cpp` | Price ↔ slot mapping and best-level tracking |
| `src/snapshot.cpp` | Binary snapshot (`snapshot.bin`): one 48-byte big-endian BBO packet per ITCH message, per `hardware-orderbook/spec/snapshot.yaml` (v3). Byte-identical to the RTL's output — this is the scoreboard trace |
| `src/trace.cpp` | `trace.txt` (per message: outcome, memory writes, BBO) and `state.txt` (end-of-run dump of every table entry and occupied level) |
| `src/golden_api.cpp` | `extern "C"` plain-integer API (`libgolden_api`) for testbenches: DPI-C from UVM, or ctypes from cocotb |
| `tools/itch_to_mold.py` | Frames a Nasdaq ITCH 5.0 BinaryFILE (sample/historical download, `.gz` read directly) into a MoldUDP64 packet stream, optionally filtered by `--symbol` / `--book-only` |
| `tools/ob.py` | One-command workflow by symbol: `run`, `book`, `bbo`, `symbols`, `test` (see Quick start) |
| `tools/show_book.py` | Human-readable views: `book trace.txt --at K` rebuilds the price ladder (levels, shares, order counts, optionally each order) after any message; `snapshot snapshot.bin` decodes the BBO packets |
| `test/gen_test.cpp` | Generates `stress_test.mold`: every message type, price-window edges, a full hash set + CAM, unknown refs, wipeout. Expected results are derived in its header |
| `test/test_*.cpp` | Unit tests: parser, order table, price book, book (incl. exact write logs), command layer, snapshot/trace, C API, MoldUDP64 |

## Quick start

Put a Nasdaq ITCH 5.0 file (e.g. `20190730.BX_ITCH_50.gz`) in `sample/`, then:

```sh
python3 tools/ob.py run SPY                # build, frame SPY from the feed, run the model
python3 tools/ob.py book SPY --at 130000   # the order book after message 130000
python3 tools/ob.py bbo SPY                # best bid/offer changes
python3 tools/ob.py symbols                # what else is in the feed, busiest first
python3 tools/ob.py test                   # build and run all tests
```

Everything is named by symbol under `sample/` (`SPY.mold`, `SPY.snapshot.bin`, `SPY.trace.txt`, `SPY.state.txt`). `python3 tools/ob.py -h` lists the options; add `alias ob="python3 /path/to/Orderbook-cpp/tools/ob.py"` to `~/.bashrc` to type just `ob`. The sections below describe the underlying commands.

## Toolchain

Built and verified with **llvm-mingw** (Clang targeting `x86_64-w64-mingw32`, msvcrt runtime), not MSVC or classic MinGW-GCC.

If building on Windows:

- **Compiler**: llvm-mingw's `clang.exe` (C) / `clang++.exe` (C++) — using `clang++.exe` for the C++ compiler is required; plain `clang.exe` won't auto-link libc++ and the build will fail at the link step with undefined `std::` symbols.
- **Runtime DLLs**: add llvm-mingw's `bin/` directory to `PATH`, or statically link (`target_link_options(... PRIVATE -static)`) to avoid runtime DLL-not-found errors (`0xC0000135`) when running built executables outside the build directory.

## Building

```sh
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Run against a MoldUDP64 binary file:

```sh
./Orderbook-cpp.exe path/to/feed.mold [snapshot_out] [--base-price <price>] [--symbol <SYM>]
```

Only the book types (A F E C X D U) reach the book; every other ITCH type is skipped (no snapshot packet). `--symbol` keeps one symbol, whose stock locate is learned from the stream's `R` stock-directory message. Without it, all symbols share one book and the model warns.

Real Nasdaq data: frame a BinaryFILE first, then run one symbol:

```sh
python3 tools/itch_to_mold.py sample/20190730.BX_ITCH_50.gz sample/SPY.mold --symbol SPY
./Orderbook-cpp.exe sample/SPY.mold sample/SPY.snapshot.bin --symbol SPY
```

For RTL stimulus, frame the file unfiltered: the RTL must skip other symbols and non-book types itself.

`snapshot.bin` carries only the best bid and offer, and `trace.txt` records memory writes, so neither reads like an order book. To see the book itself:

```sh
python3 tools/show_book.py book trace.txt --at 130000 --depth 10   # ladder after message 130000
python3 tools/show_book.py book trace.txt --orders                 # final book, with each resting order
python3 tools/show_book.py snapshot sample/SPY.snapshot.bin --changes
```

`--trace <path>` and `--state <path>` move `trace.txt` and `state.txt` out of the repo root.

`snapshot_out` defaults to `<repo>/snapshot.bin`. The base price anchors the price window and must be a multiple of the tick (100 = $0.01); it defaults to the first add's price rounded down to the tick. Compare against an RTL run only with the same base price.

Run all unit tests plus the end-to-end stress feed:

```sh
ctest --test-dir out/build/x64-debug
```

## License

MIT

## Author

Devanandan J
