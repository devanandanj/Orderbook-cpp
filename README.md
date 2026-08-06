# Orderbook-cpp

A C++ order book that speaks ITCH 5.0 over MoldUDP64.

> This is a golden model — a software reference for logic that is eventually meant to be implemented in RTL. The style reflects that constraint, not a design preference.

## Architecture

- **Flat structs, free functions** — no classes, no inheritance, no virtual dispatch.
- **No dynamic allocation** — fixed-size arrays, sized at compile time (32 orders/side).
- **ITCH 5.0 parsing** — `Add`, `Delete`, `Replace`, `Execute`, decoded from raw big-endian bytes.
- **MoldUDP64 de-framing** — unpacks a packet into its constituent ITCH messages before dispatch.
- **Bounded book with eviction** — when a side is full, an incoming `Add` evicts the worst resting order only if it's more competitive; otherwise it's discarded. Ties are broken FCFS (existing order keeps its place).
- **Warnings-as-errors** — every target builds with `/W4 /WX` (MSVC) or `-Wall -Wextra -Werror`.


## Components

| File | Purpose |
|---|---|
| `src/main.cpp` | Reads a MoldUDP64 binary file, de-frames it, dispatches each ITCH message against the order book |
| `src/itchparser.cpp` | ITCH 5.0 message parsers |
| `src/moldudp64.cpp` | MoldUDP64 packet de-framing |
| `src/orderbook.cpp` | Order book state and mutation logic (Add/Cancel/Replace/Execute, eviction, discard) |
| `src/trace.cpp` | Per-message trace logging (`trace.txt`) — book state after every mutation |
| `test/gen_test.cpp` | Generates `stress_test.mold`, a 42-message deterministic MoldUDP64 feed exercising eviction, discard, tie-break, cancel, replace, and independent bid/ask sides |


## Building

```bash
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Run against a MoldUDP64 binary file:

```bash
./Orderbook-cpp.exe path/to/feed.mold
```

Generate and run the stress test feed:

```bash
./gen_test.exe                        # produces stress_test.mold
./Orderbook-cpp.exe stress_test.mold  # -> trace.txt
```

## License

MIT

## Author

[Devanandan J](https://github.com/devanandanj)