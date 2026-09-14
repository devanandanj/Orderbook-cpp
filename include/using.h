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

/* Small aliases for counts used in the Orderbook. These are kept small
   (uint8_t) because the book size in this example is limited to 64.
*/
using Bids = std::uint8_t;
using Asks = std::uint8_t;

/* Book capacity per side. Must equal `max_orders_per_side` in the
   hardware-orderbook wire-format spec (spec/snapshot.yaml); the spec's
   test_spec.py greps this file to enforce that. Do not raise this past
   what the spec's per-packet UDP MTU check allows -- currently 90.
*/
constexpr uint8_t MAX_ORDERS_PER_SIDE = 64;