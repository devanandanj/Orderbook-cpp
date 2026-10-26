/*
   book.h
   ----------------
   The order book: the order table (ordertable.h) plus a price book per
   side (pricebook.h), and the operations ITCH messages apply to them.
   This is the golden model of the RTL book.

   Every write to the table, the CAM or a level goes through one helper
   that also appends it to the book's WriteLog. The log is therefore
   complete by construction: it is exactly what the testbench compares
   against the RTL's memory write ports, message by message. Each of
   AddOrder / DeleteOrder / ReduceOrder / ReplaceOrder starts a fresh log,
   so after any of them the log holds that one message's writes.

   The struct is ~340 KB. Give it static storage (`static Book b;`), not
   a local: the Windows default stack is 1 MB.
*/

#pragma once

#include "../include/ordertable.h"
#include "../include/pricebook.h"

/* WriteLog
   The memory writes the most recent operation caused, in order.
*/
enum class WriteTarget : uint8_t {
	Table,  // index = set * WAYS + way
	Cam,    // index = CAM entry
	Bid,    // index = slot
	Ask     // index = slot
};

struct WriteRecord {
	WriteTarget target;
	uint32_t    index;
	TableEntry  entry;   // for Table / Cam
	Level       level;   // for Bid / Ask
};

/* The worst case is a replace: remove the old order (entry + level) and
   insert the new one (entry + level). */
constexpr uint32_t MAX_WRITES_PER_MSG = 4;

struct WriteLog {
	WriteRecord records[MAX_WRITES_PER_MSG]{};
	uint32_t    count{};
};

struct Book {
	OrderTable table{};
	PriceBook  bids{};
	PriceBook  asks{};
	Price      basePrice{};
	WriteLog   log{};
	uint64_t   dropped{};          // adds lost to a full set + CAM
	uint64_t   priceRejected{};    // adds off-tick or outside the window
};

/* ResetBook
   Empties the book and sets the session's base price.
*/
void ResetBook(Book* book, Price basePrice);

/* BeginMessage
   Clears the write log. The operations below call it themselves; call it
   directly only for a message that applies no operation (so its trace
   shows no writes rather than the previous message's).
*/
void BeginMessage(Book* book);

/* AddOrder ('A', 'F')
   Stores the order and adds its shares to its level. No duplicate-ORN
   check: ITCH guarantees uniqueness and the RTL does not spend a lookup
   on it, so the model doesn't either.
*/
enum class AddResult : uint8_t {
	Inserted,
	Dropped,        // set and CAM full; book unchanged
	PriceRejected   // off-tick or outside the window; book unchanged
};

AddResult AddOrder(Book* book, const Order& order);

/* DeleteOrder ('D')
   Removes the order and all its remaining shares. Returns false (book
   unchanged) when the ORN is not in the table.
*/
bool DeleteOrder(Book* book, OrderId orderId);

/* ReduceOrder ('E', 'C', 'X')
   Takes `quantity` shares off the order, at the level the order is stored
   at -- for 'C' that is NOT the message's execution price. Reducing by the
   remaining shares or more removes the order. Returns false (book
   unchanged) when the ORN is not in the table.
*/
bool ReduceOrder(Book* book, OrderId orderId, Quantity quantity);

/* ReplaceOrder ('U')
   Removes the old order, then adds the new ORN on the OLD order's side
   ('U' carries none) at the new price and shares. If that add is dropped
   or price-rejected, the old order stays removed.
*/
enum class ReplaceResult : uint8_t {
	Replaced,
	NotFound,       // old ORN not in the table; book unchanged
	Dropped,        // old removed; new did not fit
	PriceRejected   // old removed; new price off-tick or out of window
};

ReplaceResult ReplaceOrder(Book* book, const OrderModify& mod);

/* GetBbo
   Best bid and offer. An empty side reports price, shares and orders 0.
*/
struct Bbo {
	Price    bidPrice;
	Quantity bidShares;
	uint16_t bidOrders;
	Price    askPrice;
	Quantity askShares;
	uint16_t askOrders;
};

Bbo GetBbo(const Book& book);
