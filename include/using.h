/*
   using.h
   ----------------
   Lightweight type aliases to make code clearer.

*/

#pragma once

#include <cstdint>

using Price = std::uint32_t;
using OrderId = std::uint64_t;
using Quantity = std::uint32_t;

/* Index into a side's price-level array (0 .. SLOTS-1). */
using Slot = std::uint16_t;

/* Book geometry. Each value must equal its counterpart in the
   hardware-orderbook spec (spec/book.yaml); the spec's test_spec.py greps
   this file to enforce that.
*/

/* Order table: SETS x WAYS set-associative entries, set index = XOR-fold
   of the ORN into HASH_W bits, plus a CAM_DEPTH-entry overflow CAM. */
constexpr uint32_t HASH_W = 10;
constexpr uint32_t WAYS = 3;
constexpr uint32_t CAM_DEPTH = 16;
constexpr uint32_t SETS = 1u << HASH_W;

/* Price book: SLOTS levels per side, one PRICE_TICK apart, with the
   session's base price at SLOT_CENTER. PRICE_TICK is in ITCH price units
   (4 implied decimals), so 100 = $0.01. */
constexpr uint32_t PRICE_TICK = 100;
constexpr uint32_t SLOT_W = 14;
constexpr uint32_t SLOTS = 1u << SLOT_W;
constexpr uint32_t SLOT_CENTER = SLOTS / 2;
