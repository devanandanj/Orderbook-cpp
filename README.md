# Orderbook-cpp

A C++ order book that speaks ITCH 5.0 over MoldUDP64.

> This is a golden model — a software reference for logic that is eventually meant to be implemented in RTL. The style reflects that constraint, not a design preference.

## Architecture

- **Flat structs, free functions** — no classes, no inheritance, no virtual dispatch.
- **No dynamic allocation** — fixed-size arrays, sized at compile time.
- **ITCH 5.0 parsing** — `Add`, `Delete`, `Replace`, `Execute`, decoded from raw big-endian bytes.
- **MoldUDP64 de-framing** — unpacks a packet into its constituent ITCH messages before dispatch.

## Components

| File | Purpose |
|---|---|
| `src/main.cpp` | Reads a MoldUDP64 binary file, de-frames it, dispatches each ITCH message against the order book |
| `src/itchparser.cpp` | ITCH 5.0 message parsers |
| `src/moldudp64.cpp` | MoldUDP64 packet de-framing |
| `src/orderbook.cpp` | Order book state and mutation logic |
| `test/` | Unit tests, plus a standalone MoldUDP64 test-feed generator |

## Building

```bash
cmake --preset x64-debug
cmake --build --preset x64-debug
```

Run against a MoldUDP64 binary file:

```bash
./Orderbook-cpp.exe path/to/feed.mold
```

## License

MIT

## Author

[Devanandan J](https://github.com/devanandanj)