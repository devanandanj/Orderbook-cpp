/*
   test_golden_api.cpp
   The C interface the testbenches call (include/golden_api.h): commands
   in, BBO / write log / memory reads out, all as plain integers.
*/

#include <cassert>
#include <cstdio>
#include "../include/golden_api.h"

struct Write {
	unsigned int target, index, valid, side, slot, shares, orders;
	unsigned long long oref;
};

static Write GetWrite(const int i) {
	Write w{};
	golden_write_get(i, &w.target, &w.index, &w.valid, &w.oref, &w.side, &w.slot, &w.shares, &w.orders);
	return w;
}

int main() {
	const unsigned int base = 1500000;
	const unsigned int tick = golden_tick();
	golden_reset(base);

	// Add: status 0, two writes, and the table read agrees with the log.
	assert(golden_apply('A', 1, 42, 0, 100, base + 2 * tick) == 0);
	assert(golden_write_count() == 2);
	const Write t = GetWrite(0);
	const Write l = GetWrite(1);
	assert(t.target == 0 && t.valid == 1 && t.oref == 42 && t.side == 1 && t.shares == 100);
	assert(l.target == 3 && l.index == t.slot && l.shares == 100 && l.orders == 1);

	unsigned int valid, side, slot, shares, orders;
	unsigned long long oref;
	golden_table_get(t.index, &valid, &oref, &side, &slot, &shares);
	assert(valid == 1 && oref == 42 && slot == t.slot);
	golden_level_get(1, t.slot, &shares, &orders);
	assert(shares == 100 && orders == 1);

	unsigned int bp, bs, bn, ap, as, an;
	golden_bbo(&bp, &bs, &bn, &ap, &as, &an);
	assert(bp == 0 && bs == 0 && bn == 0);
	assert(ap == base + 2 * tick && as == 100 && an == 1);

	// Status bits come straight through.
	assert(golden_apply('D', 0, 999, 0, 0, 0) == 8);         // unknown_order_ref
	assert(golden_write_count() == 0);
	assert(golden_apply('A', 0, 43, 0, 1, base + 1) == 2);   // price_rejected

	// Unsupported type: -1, and no stale writes left behind.
	assert(golden_apply('X', 0, 42, 0, 1, 0) == 0);           // a real write first
	assert(golden_write_count() == 2);
	assert(golden_apply('Q', 0, 0, 0, 0, 0) == -1);
	assert(golden_write_count() == 0);

	// Out-of-range reads are zeros, not out-of-bounds.
	const Write none = GetWrite(7);
	assert(none.target == 0 && none.oref == 0);
	golden_table_get(golden_sets() * golden_ways(), &valid, &oref, &side, &slot, &shares);
	assert(valid == 0 && oref == 0);
	golden_cam_get(golden_cam_depth(), &valid, &oref, &side, &slot, &shares);
	assert(valid == 0);
	golden_level_get(0, golden_slots(), &shares, &orders);
	assert(shares == 0 && orders == 0);

	printf("All golden_api tests PASSED\n");
	return 0;
}
