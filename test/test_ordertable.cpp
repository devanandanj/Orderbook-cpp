/*
   test_ordertable.cpp
   Order table placement and lookup rules (spec/book.yaml). These are the
   rules the RTL must reproduce slot-for-slot, so each is pinned here.
*/

#include <cassert>
#include <cstdio>
#include "../include/ordertable.h"

/* An ORN that hashes to `set`, distinct for each k < SETS: the chunk at
   bits [HASH_W, 2*HASH_W) cancels the k mixed into the low chunk. */
static OrderId OrnInSet(const uint32_t set, const uint32_t k) {
	return (static_cast<OrderId>(k) << HASH_W) | (set ^ k);
}

/* Writes an order into wherever an insert would place it. */
static TableLoc Insert(OrderTable& t, const OrderId orn) {
	const TableLoc loc = TableFindFree(t, orn);
	assert(loc.found);
	TableAt(t, loc) = TableEntry{true, orn, Side::Buy, 0, 1};
	return loc;
}

static void test_hash() {
	assert(HashOrn(0) == 0);
	assert(HashOrn(1) == 1);
	assert(HashOrn(SETS - 1) == SETS - 1);
	assert(HashOrn(1ull << HASH_W) == 1);                 // bit 10 folds onto bit 0
	assert(HashOrn(1ull | (1ull << HASH_W)) == 0);        // ...and cancels it
	assert(HashOrn(1ull << 63) == 1u << (63 % HASH_W));   // top bit -> bit 3
	// All ones: six full 10-bit chunks cancel in pairs, leaving bits 60..63.
	assert(HashOrn(~0ull) == 0xF);
	for (uint32_t k = 0; k < 50; k++) assert(HashOrn(OrnInSet(77, k)) == 77);
	printf("hash: PASS\n");
}

static void test_placement() {
	static OrderTable t;
	t = OrderTable{};
	const uint32_t s = 5;

	// Ways fill lowest-first, then the CAM.
	for (uint32_t w = 0; w < WAYS; w++) {
		const TableLoc loc = Insert(t, OrnInSet(s, w));
		assert(!loc.inCam && loc.set == s && loc.index == w);
	}
	const TableLoc spill = Insert(t, OrnInSet(s, WAYS));
	assert(spill.inCam && spill.index == 0);

	// A freed way is reused before the CAM.
	TableAt(t, TableFind(t, OrnInSet(s, 1))) = TableEntry{};
	const TableLoc reuse = TableFindFree(t, OrnInSet(s, 99));
	assert(reuse.found && !reuse.inCam && reuse.index == 1);

	// Other sets are untouched by set s's overflow.
	const TableLoc other = TableFindFree(t, OrnInSet(s + 1, 0));
	assert(!other.inCam && other.index == 0);
	printf("placement: PASS\n");
}

static void test_find() {
	static OrderTable t;
	t = OrderTable{};
	const uint32_t s = 9;
	for (uint32_t k = 0; k <= WAYS; k++) Insert(t, OrnInSet(s, k));   // last one spills

	const TableLoc inWay = TableFind(t, OrnInSet(s, 2));
	assert(inWay.found && !inWay.inCam && inWay.index == 2);
	const TableLoc inCam = TableFind(t, OrnInSet(s, WAYS));
	assert(inCam.found && inCam.inCam && inCam.index == 0);
	assert(!TableFind(t, OrnInSet(s, 50)).found);

	// Removing clears valid; the ORN is then not found and nothing moved.
	TableAt(t, inWay) = TableEntry{};
	assert(!TableFind(t, OrnInSet(s, 2)).found);
	assert(TableFind(t, OrnInSet(s, WAYS)).inCam);

	// First match wins: ways before the CAM. (Only reachable with a
	// duplicate ORN, which neither the RTL nor the model screens out.)
	const OrderId dup = OrnInSet(s, 7);
	t.cam[3] = TableEntry{true, dup, Side::Sell, 0, 1};
	t.ways[s][2] = TableEntry{true, dup, Side::Buy, 0, 1};
	const TableLoc first = TableFind(t, dup);
	assert(!first.inCam && first.index == 2);
	printf("find: PASS\n");
}

static void test_full() {
	static OrderTable t;
	t = OrderTable{};
	const uint32_t s = 300;
	for (uint32_t k = 0; k < WAYS + CAM_DEPTH; k++) Insert(t, OrnInSet(s, k));

	// Set and CAM both full: an insert has nowhere to go and is dropped...
	assert(!TableFindFree(t, OrnInSet(s, 500)).found);
	// ...but a different set still has its own ways.
	assert(TableFindFree(t, OrnInSet(s + 1, 0)).found);
	// The CAM is shared, so that other set cannot spill any more.
	for (uint32_t k = 0; k < WAYS; k++) Insert(t, OrnInSet(s + 1, k));
	assert(!TableFindFree(t, OrnInSet(s + 1, 99)).found);
	printf("full: PASS\n");
}

static void test_address() {
	assert(TableAddress(TableLoc{true, false, 4, 2}) == 4 * WAYS + 2);
	assert(TableAddress(TableLoc{true, true, 4, 7}) == 7);
	printf("address: PASS\n");
}

int main() {
	test_hash();
	test_placement();
	test_find();
	test_full();
	test_address();

	printf("\nAll ordertable tests PASSED\n");
	return 0;
}
