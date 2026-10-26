/*
   test_pricebook.cpp
   Price-window mapping and best-level tracking (spec/book.yaml).
*/

#include <cassert>
#include <cstdio>
#include "../include/pricebook.h"

// $150.00 in ITCH units: on the tick grid, and far enough above
// SLOT_CENTER ticks that the whole window is positive.
static constexpr Price BASE = 1500000;

static void Set(PriceBook& pb, const Side side, const Slot slot, const uint16_t orders) {
	pb.levels[slot] = Level{orders * 10u, orders};
	PriceBookNoteWrite(pb, side, slot);
}

static void test_price_to_slot() {
	assert(PriceToSlot(BASE, BASE) == SLOT_CENTER);
	assert(PriceToSlot(BASE, BASE + PRICE_TICK) == SLOT_CENTER + 1);
	assert(PriceToSlot(BASE, BASE - PRICE_TICK) == SLOT_CENTER - 1);

	// Window edges, and one tick past each.
	const Price lo = BASE - SLOT_CENTER * PRICE_TICK;
	const Price hi = BASE + (SLOTS - 1 - SLOT_CENTER) * PRICE_TICK;
	assert(PriceToSlot(BASE, lo) == 0);
	assert(PriceToSlot(BASE, hi) == SLOTS - 1);
	assert(!PriceToSlot(BASE, lo - PRICE_TICK).has_value());
	assert(!PriceToSlot(BASE, hi + PRICE_TICK).has_value());

	// Off the tick grid (half a cent) on either side of the base.
	assert(!PriceToSlot(BASE, BASE + 50).has_value());
	assert(!PriceToSlot(BASE, BASE - 50).has_value());

	// A low base: prices near zero still map, nothing goes negative.
	assert(PriceToSlot(100000, 0) == SLOT_CENTER - 1000);

	// SlotToPrice inverts it across the window.
	for (uint32_t s = 0; s < SLOTS; s += 997)
		assert(PriceToSlot(BASE, SlotToPrice(BASE, static_cast<Slot>(s))) == s);
	printf("price_to_slot: PASS\n");
}

static void test_best_bid() {
	static PriceBook pb;
	pb = PriceBook{};
	assert(!pb.hasBest);

	Set(pb, Side::Buy, 100, 1);
	assert(pb.hasBest && pb.best == 100);
	Set(pb, Side::Buy, 90, 1);          // worse bid: best unchanged
	assert(pb.best == 100);
	Set(pb, Side::Buy, 120, 1);         // better bid: best moves up
	assert(pb.best == 120);
	Set(pb, Side::Buy, 120, 2);         // more at the best: still best
	assert(pb.best == 120);

	Set(pb, Side::Buy, 90, 0);          // empty a non-best level: no rescan
	assert(pb.best == 120);
	Set(pb, Side::Buy, 120, 0);         // empty the best: next one down
	assert(pb.best == 100);
	Set(pb, Side::Buy, 100, 0);         // last one: side empty
	assert(!pb.hasBest);
	printf("best_bid: PASS\n");
}

static void test_best_ask() {
	static PriceBook pb;
	pb = PriceBook{};

	Set(pb, Side::Sell, 200, 1);
	Set(pb, Side::Sell, 210, 1);        // worse ask: best unchanged
	assert(pb.best == 200);
	Set(pb, Side::Sell, 190, 1);        // better ask: best moves down
	assert(pb.best == 190);
	Set(pb, Side::Sell, 190, 0);        // empty the best: next one up
	assert(pb.best == 200);

	// Rescan reaches the window edge.
	Set(pb, Side::Sell, 200, 0);
	Set(pb, Side::Sell, 210, 0);
	Set(pb, Side::Sell, SLOTS - 1, 1);
	Set(pb, Side::Sell, 0, 1);
	Set(pb, Side::Sell, 0, 0);
	assert(pb.hasBest && pb.best == SLOTS - 1);
	printf("best_ask: PASS\n");
}

int main() {
	test_price_to_slot();
	test_best_bid();
	test_best_ask();

	printf("\nAll pricebook tests PASSED\n");
	return 0;
}
