/*
   golden_api.cpp
   ----------------
   C interface to the golden model. See include/golden_api.h.

*/

#include "../include/golden_api.h"
#include "../include/book.h"
#include "../include/command.h"

namespace {

Book book;   // ~340 KB: static storage

void EntryOut(const TableEntry& e, unsigned int* valid, unsigned long long* oref,
              unsigned int* side, unsigned int* slot, unsigned int* shares) {
	*valid = e.valid ? 1 : 0;
	*oref = e.orderId;
	*side = e.side == Side::Buy ? 0 : 1;
	*slot = e.slot;
	*shares = e.shares;
}

}  // namespace

extern "C" {

void golden_reset(const unsigned int base_price) {
	ResetBook(&book, base_price);
}

int golden_apply(const unsigned char typ, const unsigned char side,
                 const unsigned long long oref, const unsigned long long new_ref,
                 const unsigned int shares, const unsigned int price) {
	switch (typ) {
	case 'A': case 'F': case 'E': case 'C': case 'X': case 'D': case 'U':
		break;
	default:
		BeginMessage(&book);
		return -1;
	}
	const Command cmd{static_cast<char>(typ), side ? Side::Sell : Side::Buy,
	                  oref, new_ref, shares, price};
	return ApplyCommand(&book, cmd).status;
}

void golden_bbo(unsigned int* bid_price, unsigned int* bid_shares, unsigned int* bid_orders,
                unsigned int* ask_price, unsigned int* ask_shares, unsigned int* ask_orders) {
	const Bbo b = GetBbo(book);
	*bid_price = b.bidPrice;
	*bid_shares = b.bidShares;
	*bid_orders = b.bidOrders;
	*ask_price = b.askPrice;
	*ask_shares = b.askShares;
	*ask_orders = b.askOrders;
}

int golden_write_count(void) {
	return static_cast<int>(book.log.count);
}

void golden_write_get(const int i, unsigned int* target, unsigned int* index,
                      unsigned int* valid, unsigned long long* oref, unsigned int* side,
                      unsigned int* slot, unsigned int* shares, unsigned int* orders) {
	*target = *index = *valid = *side = *slot = *shares = *orders = 0;
	*oref = 0;
	if (i < 0 || static_cast<uint32_t>(i) >= book.log.count) return;

	const WriteRecord& r = book.log.records[i];
	*target = static_cast<unsigned int>(r.target);
	*index = r.index;
	if (r.target == WriteTarget::Table || r.target == WriteTarget::Cam) {
		EntryOut(r.entry, valid, oref, side, slot, shares);
	}
	else {
		*shares = r.level.shares;
		*orders = r.level.orders;
	}
}

void golden_table_get(const unsigned int addr, unsigned int* valid, unsigned long long* oref,
                      unsigned int* side, unsigned int* slot, unsigned int* shares) {
	const TableEntry e = addr < SETS * WAYS ? book.table.ways[addr / WAYS][addr % WAYS] : TableEntry{};
	EntryOut(e, valid, oref, side, slot, shares);
}

void golden_cam_get(const unsigned int entry, unsigned int* valid, unsigned long long* oref,
                    unsigned int* side, unsigned int* slot, unsigned int* shares) {
	const TableEntry e = entry < CAM_DEPTH ? book.table.cam[entry] : TableEntry{};
	EntryOut(e, valid, oref, side, slot, shares);
}

void golden_level_get(const unsigned int side, const unsigned int slot,
                      unsigned int* shares, unsigned int* orders) {
	const PriceBook& pb = side ? book.asks : book.bids;
	const Level l = slot < SLOTS ? pb.levels[slot] : Level{};
	*shares = l.shares;
	*orders = l.orders;
}

unsigned int golden_sets(void) { return SETS; }
unsigned int golden_ways(void) { return WAYS; }
unsigned int golden_cam_depth(void) { return CAM_DEPTH; }
unsigned int golden_slots(void) { return SLOTS; }
unsigned int golden_tick(void) { return PRICE_TICK; }

}  // extern "C"
