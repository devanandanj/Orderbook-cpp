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
   (uint8_t) because the book size in this example is limited to 32.
*/
using Bids = std::uint8_t;
using Asks = std::uint8_t;
