/*
   book.cpp
   ----------------
   Order book operations. See include/book.h.

*/

#include "../include/book.h"

#include <cassert>

static PriceBook& SideBook(Book* book, const Side side) {
	return side == Side::Buy ? book->bids : book->asks;
}

static void Log(Book* book, const WriteRecord& rec) {
	// MAX_WRITES_PER_MSG is the proven worst case (replace); exceeding it
	// is a model bug, not a feed condition.
	assert(book->log.count < MAX_WRITES_PER_MSG);
	if (book->log.count < MAX_WRITES_PER_MSG)
		book->log.records[book->log.count++] = rec;
}

/* WriteEntry / WriteLevel
   The only places the table or a level is written, so every write is
   logged.
*/
static void WriteEntry(Book* book, const TableLoc& loc, const TableEntry& entry) {
	TableAt(book->table, loc) = entry;
	WriteRecord rec{};
	rec.target = loc.inCam ? WriteTarget::Cam : WriteTarget::Table;
	rec.index = TableAddress(loc);
	rec.entry = entry;
	Log(book, rec);
}

static void WriteLevel(Book* book, const Side side, const Slot slot, const Level& level) {
	PriceBook& pb = SideBook(book, side);
	pb.levels[slot] = level;
	PriceBookNoteWrite(pb, side, slot);
	WriteRecord rec{};
	rec.target = side == Side::Buy ? WriteTarget::Bid : WriteTarget::Ask;
	rec.index = slot;
	rec.level = level;
	Log(book, rec);
}

/* RemoveAt
   Takes a found order out of the book entirely: clear its entry, then
   take its remaining shares and its count off its level.
*/
static void RemoveAt(Book* book, const TableLoc& loc) {
	const TableEntry e = TableAt(book->table, loc);
	WriteEntry(book, loc, TableEntry{});

	Level lvl = SideBook(book, e.side).levels[e.slot];
	lvl.shares -= e.shares;
	lvl.orders--;
	WriteLevel(book, e.side, e.slot, lvl);
}

void ResetBook(Book* book, const Price basePrice) {
	*book = Book{};
	book->basePrice = basePrice;
}

void BeginMessage(Book* book) {
	book->log.count = 0;
}

/* InsertOrder
   AddOrder without starting a new write log, so ReplaceOrder's add lands
   in the same log as its remove.
*/
static AddResult InsertOrder(Book* book, const Order& order) {
	const std::optional<Slot> slot = PriceToSlot(book->basePrice, order.price);
	if (!slot) {
		book->priceRejected++;
		return AddResult::PriceRejected;
	}

	const TableLoc loc = TableFindFree(book->table, order.orderId);
	if (!loc.found) {
		book->dropped++;
		return AddResult::Dropped;
	}

	WriteEntry(book, loc, TableEntry{true, order.orderId, order.side, *slot, order.quantity});

	Level lvl = SideBook(book, order.side).levels[*slot];
	lvl.shares += order.quantity;
	lvl.orders++;
	WriteLevel(book, order.side, *slot, lvl);
	return AddResult::Inserted;
}

AddResult AddOrder(Book* book, const Order& order) {
	BeginMessage(book);
	return InsertOrder(book, order);
}

bool DeleteOrder(Book* book, const OrderId orderId) {
	BeginMessage(book);
	const TableLoc loc = TableFind(book->table, orderId);
	if (!loc.found) return false;
	RemoveAt(book, loc);
	return true;
}

bool ReduceOrder(Book* book, const OrderId orderId, const Quantity quantity) {
	BeginMessage(book);
	const TableLoc loc = TableFind(book->table, orderId);
	if (!loc.found) return false;

	TableEntry e = TableAt(book->table, loc);
	if (quantity >= e.shares) {
		RemoveAt(book, loc);
		return true;
	}

	e.shares -= quantity;
	WriteEntry(book, loc, e);

	Level lvl = SideBook(book, e.side).levels[e.slot];
	lvl.shares -= quantity;
	WriteLevel(book, e.side, e.slot, lvl);
	return true;
}

ReplaceResult ReplaceOrder(Book* book, const OrderModify& mod) {
	BeginMessage(book);
	const TableLoc loc = TableFind(book->table, mod.oldOrderId);
	if (!loc.found) return ReplaceResult::NotFound;

	const Side side = TableAt(book->table, loc).side;
	RemoveAt(book, loc);

	switch (InsertOrder(book, Order{mod.newOrderId, side, mod.price, mod.quantity})) {
		case AddResult::Inserted: return ReplaceResult::Replaced;
		case AddResult::Dropped:  return ReplaceResult::Dropped;
		default:                  return ReplaceResult::PriceRejected;
	}
}

Bbo GetBbo(const Book& book) {
	Bbo bbo{};
	if (book.bids.hasBest) {
		const Level& l = book.bids.levels[book.bids.best];
		bbo.bidPrice = SlotToPrice(book.basePrice, book.bids.best);
		bbo.bidShares = l.shares;
		bbo.bidOrders = l.orders;
	}
	if (book.asks.hasBest) {
		const Level& l = book.asks.levels[book.asks.best];
		bbo.askPrice = SlotToPrice(book.basePrice, book.asks.best);
		bbo.askShares = l.shares;
		bbo.askOrders = l.orders;
	}
	return bbo;
}
