# Orderbook-cpp

A C++ order book that speaks ITCH 5.0 over MoldUDP64.

This is a golden model — a software reference for logic that is eventually meant to be implemented in RTL. The style reflects that constraint, not a design preference.

## Architecture

- **Flat structs, free functions** — no classes, no inheritance, no virtual dispatch.
- **No dynamic allocation** — fixed-size arrays, sized at compile time (64 orders/side).
- **ITCH 5.0 parsing** — Add, Delete, Replace, Execute, decoded from raw big-endian bytes.
- **MoldUDP64 de-framing** — unpacks a packet into its constituent ITCH messages before dispatch.
- **Bounded book with eviction** — when a side is full, an incoming Add evicts the worst resting order only if it's more competitive; otherwise it's discarded. Ties are broken FCFS (existing order keeps its place).
- **Unknown refs are flagged, not fatal** — a D/U/E for an order the bounded book evicted or discarded sets `unknown_order_ref` and leaves the book unchanged, matching the RTL.
- **Warnings-as-errors** — every target builds with `/W4 /WX` (MSVC) or `-Wall -Wextra -Werror`, applied uniformly via a shared CMake helper.

## Components

| File | Purpose |
|---|---|
| `src/main.cpp` | Reads a MoldUDP64 binary file, de-frames it, dispatches each ITCH message against the order book |
| `src/itchparser.cpp` | ITCH 5.0 message parsers |
| `src/moldudp64.cpp` | MoldUDP64 packet de-framing |
| `src/orderbook.cpp` | Order book state and mutation logic (Add/Cancel/Replace/Execute, eviction, discard) |
| `src/snapshot.cpp` | Binary book snapshot (`snapshot.bin`): two 1056-byte big-endian packets (bids, asks) per ITCH message, per `hardware-orderbook/spec/snapshot.yaml`. Byte-identical to the RTL's output — this is the scoreboard trace |
| `src/trace.cpp` | Per-message text dump (`trace.txt`) — debug aid for eyeballing the book after each mutation. Not the model's product output; the model's job is to maintain the in-memory book state correctly (see `include/orderbook.h`). |
| `test/gen_test.cpp` | Generates `stress_test.mold`, a deterministic MoldUDP64 feed scaled to `MAX_ORDERS_PER_SIDE`, exercising eviction, discard, tie-break, cancel, wipeout, and unknown refs on both sides |
| `test/test_snapshot.cpp` | Snapshot encoder byte-layout tests |
| `test/test_orderbook.cpp` | Order book unit tests |
| `test/test_itchparser.cpp` | ITCH parser unit tests |
| `test/test_moldudp64.cpp` | MoldUDP64 de-framer unit tests |

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
./Orderbook-cpp.exe path/to/feed.mold [snapshot_out]   # default: <repo>/snapshot.bin
```

Run all unit tests plus the end-to-end stress feed:

```sh
ctest --test-dir out/build/x64-debug
```

## License

MIT

## Author

Devanandan J
