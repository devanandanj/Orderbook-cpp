/*
   pricebook.h
   ----------------
   One side's price levels: a {shares, orders} counter per price slot, and
   the best occupied slot (highest for bids, lowest for asks).

   Price <-> slot (spec/book.yaml):
       slot = (price - basePrice) / PRICE_TICK + SLOT_CENTER
   A price off the tick grid or outside [0, SLOTS) has no slot; the book
   rejects such an order instead of storing it.

   Best tracking mirrors the RTL: a write that occupies a better slot moves
   best there; emptying the best slot rescans away from it to the next
   occupied one (the RTL does that with a priority encoder over occupancy
   bits). Every other write leaves best alone.

   Like the order table, this module never writes levels itself: the book
   writes (and logs) the level, then calls PriceBookNoteWrite.
*/

#pragma once

#include <optional>

#include "../include/using.h"
#include "../include/types.h"

/* Level
   Total resting shares at one price and how many orders make them up. A
   level is empty exactly when orders == 0.
*/
struct Level {
	Quantity shares;
	uint16_t orders;
};

struct PriceBook {
	Level levels[SLOTS]{};
	Slot  best{};
	bool  hasBest{};
};

/* PriceToSlot / SlotToPrice
   Map between ITCH prices and slots for a session base price. The base
   should itself be on the tick grid so that every real tick has a slot.
   SlotToPrice is only meaningful for a slot PriceToSlot can produce.
*/
std::optional<Slot> PriceToSlot(Price basePrice, Price price);
Price SlotToPrice(Price basePrice, Slot slot);

/* IsBetter
   True when slot a is a more competitive price than slot b on `side`.
   Slots rise with price, so this is a plain compare.
*/
bool IsBetter(Side side, Slot a, Slot b);

/* PriceBookNoteWrite
   Updates best after levels[slot] has been written.
*/
void PriceBookNoteWrite(PriceBook& pb, Side side, Slot slot);
