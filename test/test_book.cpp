/*
   test_book.cpp
   Message semantics of the book (docs/redesign-plan.md) and the exact
   write log each operation produces -- the log is what the testbench
   checks against the RTL's memory write ports, so its order and contents
   are part of the contract.
*/

#include <cassert>
#include <cstdio>
#include "../include/book.h"

static constexpr Price BASE = 1500000;   // $150.00
static constexpr Price TICK = PRICE_TICK;

static Book book;

static void Fresh() {
	ResetBook(&book, BASE);
}

static Slot SlotOf(const Price p) { return *PriceToSlot(BASE, p); }

static const WriteRecord& W(const uint32_t i) {
	assert(i < book.log.count);
	return book.log.records[i];
}

/* An ORN that hashes to `set`, distinct for each k < SETS. */
static OrderId OrnInSet(const uint32_t set, const uint32_t k) {
	return (static_cast<OrderId>(k) << HASH_W) | (set ^ k);
}

static void test_add_and_bbo() {
	Fresh();
	assert(AddOrder(&book, Order{1, Side::Buy, BASE, 100}) == AddResult::Inserted);

	// Two writes: the table entry, then the level.
	assert(book.log.count == 2);
	assert(W(0).target == WriteTarget::Table && W(0).index == HashOrn(1) * WAYS + 0);
	assert(W(0).entry.valid && W(0).entry.orderId == 1 && W(0).entry.slot == SLOT_CENTER
		&& W(0).entry.shares == 100 && W(0).entry.side == Side::Buy);
	assert(W(1).target == WriteTarget::Bid && W(1).index == SLOT_CENTER);
	assert(W(1).level.shares == 100 && W(1).level.orders == 1);

	// Same level aggregates; a better bid and an ask set the BBO.
	AddOrder(&book, Order{2, Side::Buy, BASE, 50});
	assert(book.log.count == 2 && W(1).level.shares == 150 && W(1).level.orders == 2);
	AddOrder(&book, Order{3, Side::Buy, BASE + TICK, 7});
	AddOrder(&book, Order{4, Side::Sell, BASE + 5 * TICK, 9});

	const Bbo bbo = GetBbo(book);
	assert(bbo.bidPrice == BASE + TICK && bbo.bidShares == 7 && bbo.bidOrders == 1);
	assert(bbo.askPrice == BASE + 5 * TICK && bbo.askShares == 9 && bbo.askOrders == 1);
	printf("add_and_bbo: PASS\n");
}

static void test_empty_bbo() {
	Fresh();
	const Bbo bbo = GetBbo(book);
	assert(bbo.bidPrice == 0 && bbo.bidShares == 0 && bbo.bidOrders == 0);
	assert(bbo.askPrice == 0 && bbo.askShares == 0 && bbo.askOrders == 0);
	printf("empty_bbo: PASS\n");
}

static void test_price_rejected() {
	Fresh();
	assert(AddOrder(&book, Order{1, Side::Buy, BASE + 50, 10}) == AddResult::PriceRejected);
	assert(AddOrder(&book, Order{2, Side::Sell, BASE + SLOTS * TICK, 10}) == AddResult::PriceRejected);
	assert(book.priceRejected == 2 && book.log.count == 0);
	// Not stored, so later references are unknown.
	assert(!DeleteOrder(&book, 1));
	printf("price_rejected: PASS\n");
}

static void test_dropped() {
	Fresh();
	const uint32_t s = 42;
	for (uint32_t k = 0; k < WAYS + CAM_DEPTH; k++)
		assert(AddOrder(&book, Order{OrnInSet(s, k), Side::Buy, BASE, 1}) == AddResult::Inserted);

	// The 4th order of the set went to CAM entry 0.
	assert(TableFind(book.table, OrnInSet(s, WAYS)).inCam);

	assert(AddOrder(&book, Order{OrnInSet(s, 99), Side::Buy, BASE, 1}) == AddResult::Dropped);
	assert(book.dropped == 1 && book.log.count == 0);
	assert(book.bids.levels[SLOT_CENTER].orders == WAYS + CAM_DEPTH);

	// Deleting a CAM-resident order frees its CAM entry for the next spill.
	const OrderId inCam = OrnInSet(s, WAYS + 2);
	const uint32_t camIdx = TableFind(book.table, inCam).index;
	assert(DeleteOrder(&book, inCam));
	assert(W(0).target == WriteTarget::Cam && W(0).index == camIdx && !W(0).entry.valid);
	const TableLoc again = TableFindFree(book.table, OrnInSet(s, 100));
	assert(again.found && again.inCam && again.index == camIdx);
	printf("dropped: PASS\n");
}

static void test_delete_rescans_best() {
	Fresh();
	AddOrder(&book, Order{1, Side::Sell, BASE + 3 * TICK, 10});
	AddOrder(&book, Order{2, Side::Sell, BASE + 8 * TICK, 20});
	assert(GetBbo(book).askPrice == BASE + 3 * TICK);

	assert(DeleteOrder(&book, 1));
	assert(book.log.count == 2);
	assert(W(0).target == WriteTarget::Table && !W(0).entry.valid);
	assert(W(1).target == WriteTarget::Ask && W(1).index == SlotOf(BASE + 3 * TICK));
	assert(W(1).level.shares == 0 && W(1).level.orders == 0);
	assert(GetBbo(book).askPrice == BASE + 8 * TICK && GetBbo(book).askShares == 20);

	assert(!DeleteOrder(&book, 1));                  // already gone
	assert(book.log.count == 0);
	printf("delete_rescans_best: PASS\n");
}

static void test_reduce() {
	Fresh();
	AddOrder(&book, Order{1, Side::Buy, BASE, 10});
	AddOrder(&book, Order{2, Side::Buy, BASE, 5});

	// Partial: entry and level both drop by 3; order count unchanged.
	assert(ReduceOrder(&book, 1, 3));
	assert(book.log.count == 2);
	assert(W(0).entry.valid && W(0).entry.shares == 7);
	assert(W(1).level.shares == 12 && W(1).level.orders == 2);

	// Exactly the remainder removes the order.
	assert(ReduceOrder(&book, 1, 7));
	assert(!W(0).entry.valid && W(1).level.shares == 5 && W(1).level.orders == 1);
	assert(!TableFind(book.table, 1).found);

	// More than the remainder (feed overfill) also removes, never wraps.
	assert(ReduceOrder(&book, 2, 999));
	assert(W(1).level.shares == 0 && W(1).level.orders == 0);
	assert(!book.bids.hasBest);

	assert(!ReduceOrder(&book, 2, 1));               // unknown ref
	printf("reduce: PASS\n");
}

static void test_replace() {
	Fresh();
	AddOrder(&book, Order{1, Side::Sell, BASE + 2 * TICK, 10});

	// Side comes from the old order; four writes in a fixed order.
	assert(ReplaceOrder(&book, OrderModify{1, 2, BASE + 4 * TICK, 30}) == ReplaceResult::Replaced);
	assert(book.log.count == 4);
	assert(!W(0).entry.valid);                                        // old entry
	assert(W(1).target == WriteTarget::Ask && W(1).level.orders == 0);   // old level
	assert(W(2).entry.valid && W(2).entry.orderId == 2 && W(2).entry.side == Side::Sell);
	assert(W(3).target == WriteTarget::Ask && W(3).index == SlotOf(BASE + 4 * TICK));
	assert(GetBbo(book).askPrice == BASE + 4 * TICK && GetBbo(book).askShares == 30);
	assert(!TableFind(book.table, 1).found);

	// Unknown old ref: nothing happens.
	assert(ReplaceOrder(&book, OrderModify{1, 3, BASE, 1}) == ReplaceResult::NotFound);
	assert(book.log.count == 0 && TableFind(book.table, 2).found);

	// New price outside the window: the old order is still gone.
	assert(ReplaceOrder(&book, OrderModify{2, 3, BASE + 1, 1}) == ReplaceResult::PriceRejected);
	assert(book.log.count == 2);
	assert(!TableFind(book.table, 2).found && !TableFind(book.table, 3).found);
	assert(!book.asks.hasBest);
	printf("replace: PASS\n");
}

static void test_replace_into_full_set() {
	Fresh();
	const uint32_t s = 7;
	for (uint32_t k = 0; k < WAYS + CAM_DEPTH; k++)
		AddOrder(&book, Order{OrnInSet(s, k), Side::Buy, BASE, 1});
	AddOrder(&book, Order{1000, Side::Buy, BASE - TICK, 5});    // set 1000 % 1024, not s

	// The new ORN lands in the full set: old removed, new dropped.
	assert(ReplaceOrder(&book, OrderModify{1000, OrnInSet(s, 500), BASE, 5}) == ReplaceResult::Dropped);
	assert(book.log.count == 2 && book.dropped == 1);
	assert(!TableFind(book.table, 1000).found);
	printf("replace_into_full_set: PASS\n");
}

int main() {
	test_add_and_bbo();
	test_empty_bbo();
	test_price_rejected();
	test_dropped();
	test_delete_rescans_best();
	test_reduce();
	test_replace();
	test_replace_into_full_set();

	printf("\nAll book tests PASSED\n");
	return 0;
}
