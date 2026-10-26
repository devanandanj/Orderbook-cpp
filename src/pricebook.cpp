/*
   pricebook.cpp
   ----------------
   Price <-> slot mapping and best-level tracking. See include/pricebook.h.

*/

#include "../include/pricebook.h"

std::optional<Slot> PriceToSlot(const Price basePrice, const Price price) {
	const int64_t delta = static_cast<int64_t>(price) - static_cast<int64_t>(basePrice);
	if (delta % PRICE_TICK != 0) return std::nullopt;
	const int64_t slot = delta / PRICE_TICK + SLOT_CENTER;
	if (slot < 0 || slot >= SLOTS) return std::nullopt;
	return static_cast<Slot>(slot);
}

Price SlotToPrice(const Price basePrice, const Slot slot) {
	const int64_t delta = (static_cast<int64_t>(slot) - SLOT_CENTER) * PRICE_TICK;
	return static_cast<Price>(static_cast<int64_t>(basePrice) + delta);
}

bool IsBetter(const Side side, const Slot a, const Slot b) {
	return side == Side::Buy ? a > b : a < b;
}

/* RescanBest
   The best level just emptied: walk away from it -- down for bids, up for
   asks -- to the next occupied slot, or mark the side empty.
*/
static void RescanBest(PriceBook& pb, const Side side) {
	if (side == Side::Buy) {
		for (uint32_t s = pb.best; s-- > 0;)
			if (pb.levels[s].orders != 0) { pb.best = static_cast<Slot>(s); return; }
	}
	else {
		for (uint32_t s = pb.best + 1u; s < SLOTS; s++)
			if (pb.levels[s].orders != 0) { pb.best = static_cast<Slot>(s); return; }
	}
	pb.hasBest = false;
	pb.best = 0;
}

void PriceBookNoteWrite(PriceBook& pb, const Side side, const Slot slot) {
	if (pb.levels[slot].orders != 0) {
		if (!pb.hasBest || IsBetter(side, slot, pb.best)) {
			pb.best = slot;
			pb.hasBest = true;
		}
	}
	else if (pb.hasBest && pb.best == slot) {
		RescanBest(pb, side);
	}
}
