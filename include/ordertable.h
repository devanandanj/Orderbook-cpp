/*
   ordertable.h
   ----------------
   The order table: ORN -> {side, price slot, shares}. Every D/U/E/C/X
   finds its order here, and it is the one place 'U' recovers the side.

   Layout and rules are owned by hardware-orderbook/spec/book.yaml and are
   mirrored BIT-EXACTLY from the RTL, so the same order lands in the same
   slot -- and is dropped in the same case -- in both:

   - SETS sets of WAYS entries; set = HashOrn(orn).
   - A CAM_DEPTH-entry fully-associative overflow CAM shared by all sets.
   - Free slot for an insert: lowest empty way of the set, else the
     lowest free CAM entry, else none (the caller drops the order).
   - Lookup: ways 0..WAYS-1 of the set, then CAM 0..CAM_DEPTH-1; first
     valid match wins.
   - Remove clears `valid`; nothing moves. (Compacting would make RTL
     slots depend on history the model would also have to replay.)

   This module only locates slots; writing them is the book's job, so
   every write can be logged in one place (see book.h).
*/

#pragma once

#include "../include/using.h"
#include "../include/types.h"

/* TableEntry
   One table or CAM entry. Stores the price SLOT rather than the 32-bit
   price: it is what every later message needs, and 14 bits is what the
   RTL stores.
*/
struct TableEntry {
	bool     valid;
	OrderId  orderId;
	Side     side;
	Slot     slot;
	Quantity shares;
};

struct OrderTable {
	TableEntry ways[SETS][WAYS]{};
	TableEntry cam[CAM_DEPTH]{};
};

/* TableLoc
   A slot in the table: way `index` of set `set`, or CAM entry `index`.
   `set` is the ORN's hashed set either way.
*/
struct TableLoc {
	bool     found;
	bool     inCam;
	uint32_t set;
	uint32_t index;
};

/* HashOrn
   XOR-fold of the 64-bit ORN into HASH_W bits: ORN bit i is XORed into
   set-index bit (i % HASH_W). Inline so the stress generator can craft
   colliding ORNs with exactly the function the table uses.
*/
inline uint32_t HashOrn(const OrderId orn) {
	uint32_t h = 0;
	for (uint32_t shift = 0; shift < 64; shift += HASH_W)
		h ^= static_cast<uint32_t>(orn >> shift);
	return h & (SETS - 1);
}

/* TableFind
   Locates the valid entry holding `orn`. found == false when absent.
*/
TableLoc TableFind(const OrderTable& table, OrderId orn);

/* TableFindFree
   Locates where an insert of `orn` would go. found == false when the set
   and the CAM are both full.
*/
TableLoc TableFindFree(const OrderTable& table, OrderId orn);

/* TableAt
   The entry a TableLoc names. Only meaningful when loc.found.
*/
TableEntry& TableAt(OrderTable& table, const TableLoc& loc);
const TableEntry& TableAt(const OrderTable& table, const TableLoc& loc);

/* TableAddress
   Flat address of a slot as the RTL's memories see it: set*WAYS + way for
   the table, the entry number for the CAM.
*/
uint32_t TableAddress(const TableLoc& loc);
