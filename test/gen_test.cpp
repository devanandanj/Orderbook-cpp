/*
   test.cpp
   ----------------
   Standalone MoldUDP64 test-feed generator. Not part of the main
   Orderbook-cpp pipeline -- has its own main(), so it must be built as
   a SEPARATE executable target (see CMakeLists.txt note at the bottom
   of this file). Do NOT add this to the same target as src/main.cpp.

   This particular scenario is a stress test for the 32-order-per-side
   capacity + eviction logic added to AddOrder/ModifyOrder. It builds a
   single deterministic message sequence that walks through every new
   code path so the resulting trace.txt can be checked by hand,
   message-by-message, against the table in the commit/PR description.

   Message-by-message plan (see README table for the human-readable
   version -- kept in sync here as inline comments):
	 0-31   Add 32 Buy orders (id 1..32), strictly increasing price.
			Fills the bid side exactly to capacity.
			id1 = worst (lowest price), id32 = best (highest price).
	 32     Add Buy id33, price higher than everyone -> expect EVICTED,
			victim should be id1 (the worst).
	 33     Add Buy id34, price lower than the new worst (id2) ->
			expect DISCARDED (not competitive enough).
	 34     Add Buy id35, price exactly ties the current worst (id2) ->
			expect DISCARDED (first-come-first-served: id2 keeps its
			place on a tie).
	 35     Cancel id2 -> frees one bid slot (count 32 -> 31).
	 36     Add Buy id36, deliberately very low price, but there is now
			room -> expect INSERTED normally. This proves the
			competitiveness check only applies when the side is full.
	 37     Execute id36, partial fill -> order stays resting with a
			reduced quantity.
	 38     Replace id36 -> id200 -> expect REPLACED (room is
			available, so this is a plain cancel+insert, no eviction
			needed).
	 39-41  Add 3 Sell orders (id101 @ 2,000,000, id102 @ 1,990,000,
			id103 @ 2,010,000) -> sanity check that the ask side is
			completely unaffected by everything that happened on the
			bid side (sides are independent 32-slot pools).

	 --- Ask-side symmetry tests (mirrors the bid-side coverage above,
	 exercising FindWorstIndex's ask branch: highest price = worst) ---

	 42-70  Top up asks from 3 to 32 with ids 104..132, strictly
			DECREASING price (1,980,000 down to 1,952,000). Room for
			exactly 29 more (3+29=32), so this loop fills the side
			WITHOUT triggering eviction -- id104 is the loop's worst,
			id132 its best, but neither is the book's overall worst
			once id101/102/103 are accounted for.
			Book's true worst after this loop is id103 @ 2,010,000
			(highest price of all 32 resting asks).
	 71     Add id400, price lower than everyone -> expect EVICTED,
			victim should be id103 (the true worst, not the loop's
			nominal worst -- id101/103 predate the loop and were never
			displaced by it).
	 72     Add id401, price above the new worst (id101 @ 2,000,000,
			the next-worst once id103 is gone) -> expect DISCARDED.
	 73     Add id402, price exactly ties the current worst (id101) ->
			expect DISCARDED (FCFS: id101 keeps its place on a tie).
	 74     Cancel id101 -> frees one ask slot (32 -> 31).
	 75     Add id403, deliberately uncompetitive price, room now free
			-> expect INSERTED (competitiveness check only applies
			when the side is full).
	 76     Execute id403, full fill (qty 5 -> 0) -> order removed,
			ask_count drops 32 -> 31.
	 77     Add id600 on the BID side -> independence check. Bids are
			already full and unaffected by anything above; confirms
			this ask-side sequence never leaked into bid state.

   Expected final book state printed by Orderbook-cpp: bids=32 asks=31.
*/

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <vector>
#include <fstream>
#include <iostream>

#include "../include/itchparser.h"   // for ADD_ORDER_LEN etc. -- keeps
// this generator's message sizes
// locked to the same constants
// the parser checks against, so
// the two can never silently drift
// apart.

using Bytes = std::vector<uint8_t>;

static void write_u16_be(Bytes& buf, uint16_t v) {
	buf.push_back(uint8_t(v >> 8));
	buf.push_back(uint8_t(v & 0xFF));
}

static void write_u32_be(Bytes& buf, uint32_t v) {
	write_u16_be(buf, uint16_t(v >> 16));
	write_u16_be(buf, uint16_t(v & 0xFFFF));
}

static void write_u64_be(Bytes& buf, uint64_t v) {
	write_u32_be(buf, uint32_t(v >> 32));
	write_u32_be(buf, uint32_t(v & 0xFFFFFFFFu));
}

/* build_add
   Layout matches parse_add's offsets exactly:
   [0]      type 'A'
   [1..10]  unused (10 bytes)
   [11..18] orderId (u64 BE)
   [19]     side 'B' or 'S'
   [20..23] quantity (u32 BE)
   [24..31] unused (8 bytes)
   [32..35] price (u32 BE)
   total: 36 bytes == ADD_ORDER_LEN
*/
static Bytes build_add(uint64_t orderId, char side, uint32_t qty, uint32_t price) {
	Bytes m;
	m.push_back('A');
	for (std::size_t i = 0; i < 10; i++) m.push_back(0);
	write_u64_be(m, orderId);
	m.push_back(uint8_t(side));
	write_u32_be(m, qty);
	for (std::size_t i = 0; i < 8; i++) m.push_back(0);
	write_u32_be(m, price);
	assert(m.size() == ADD_ORDER_LEN);
	return m;
}

/* build_delete
   [0] 'D', [1..10] unused, [11..18] orderId (u64 BE)
   total: 19 bytes == ORDER_DELETE_LEN
*/
static Bytes build_delete(uint64_t orderId) {
	Bytes m;
	m.push_back('D');
	for (std::size_t i = 0; i < 10; i++) m.push_back(0);
	write_u64_be(m, orderId);
	assert(m.size() == ORDER_DELETE_LEN);
	return m;
}

/* build_replace
   [0] 'U', [1..10] unused,
   [11..18] OldOrderId (u64 BE), [19..26] NewOrderId (u64 BE),
   [27..30] quantity (u32 BE), [31..34] price (u32 BE)
   total: 35 bytes == ORDER_REPLACE_LEN
*/
static Bytes build_replace(uint64_t oldId, uint64_t newId, uint32_t qty, uint32_t price) {
	Bytes m;
	m.push_back('U');
	for (std::size_t i = 0; i < 10; i++) m.push_back(0);
	write_u64_be(m, oldId);
	write_u64_be(m, newId);
	write_u32_be(m, qty);
	write_u32_be(m, price);
	assert(m.size() == ORDER_REPLACE_LEN);
	return m;
}

/* build_execute
   [0] 'E', [1..10] unused,
   [11..18] orderId (u64 BE), [19..22] executedQuantity (u32 BE),
   [23..30] matchId (u64 BE)
   total: 31 bytes == ORDER_EXECUTE_LEN
*/
static Bytes build_execute(uint64_t orderId, uint32_t execQty, uint64_t matchId) {
	Bytes m;
	m.push_back('E');
	for (std::size_t i = 0; i < 10; i++) m.push_back(0);
	write_u64_be(m, orderId);
	write_u32_be(m, execQty);
	write_u64_be(m, matchId);
	assert(m.size() == ORDER_EXECUTE_LEN);
	return m;
}

/* build_mold_file
   Wraps a list of inner messages in a MoldUDP64 envelope:
   [0..9]   session id (10 bytes, zero-filled -- arbitrary for a
			file-based test feed)
   [10..17] sequence number (8 bytes, zero-filled)
   [18..19] message count (u16 BE)
   then, per message: [2-byte length][message bytes]
*/
static Bytes build_mold_file(const std::vector<Bytes>& messages) {
	Bytes buf;
	for (std::size_t i = 0; i < 10; i++) buf.push_back(0);   // session id
	for (std::size_t i = 0; i < 8; i++) buf.push_back(0);    // sequence number
	write_u16_be(buf, uint16_t(messages.size()));    // message count

	for (const Bytes& m : messages) {
		write_u16_be(buf, uint16_t(m.size()));
		buf.insert(buf.end(), m.begin(), m.end());
	}
	return buf;
}

int main() {
	std::vector<Bytes> messages;

	// --- 0-31: fill the bid side to exactly 32 orders -----------------
	// Prices strictly increasing -> id1 is worst (lowest), id32 is best.
	for (uint64_t id = 1; id <= 32; id++) {
		uint32_t price = 1000000 + uint32_t(id - 1) * 1000;
		messages.push_back(build_add(id, 'B', 10, price));
	}

	// --- 32: Add id33, higher price than everyone -> expect EVICTED ---
	// Victim should be id1 (worst / lowest price).
	messages.push_back(build_add(33, 'B', 10, 1050000));

	// --- 33: Add id34, price below the new worst (id2 @ 1001000) ------
	// Expect DISCARDED (not competitive).
	messages.push_back(build_add(34, 'B', 10, 900000));

	// --- 34: Add id35, price exactly ties the current worst (id2) -----
	// Expect DISCARDED (FCFS: id2, already resting, keeps its place).
	messages.push_back(build_add(35, 'B', 10, 1001000));

	// --- 35: Cancel id2 -> frees one bid slot (32 -> 31) ---------------
	messages.push_back(build_delete(2));

	// --- 36: Add id36, deliberately very low price, room available ----
	// Expect INSERTED (no competitiveness check applies -- side isn't
	// full at this point).
	messages.push_back(build_add(36, 'B', 5, 800000));

	// --- 37: Execute id36, partial fill (qty 5 -> 3) -------------------
	messages.push_back(build_execute(36, 2, /*matchId=*/9001));

	// --- 38: Replace id36 -> id200 -------------------------------------
	// Room is available, so this should be a plain REPLACED (no
	// eviction needed).
	messages.push_back(build_replace(36, 200, 50, 1200000));

	// --- 39-41: Add 3 Sell orders --------------------------------------
	// Control group: confirms the ask side is entirely unaffected by
	// everything that just happened on the bid side.
	messages.push_back(build_add(101, 'S', 20, 2000000));
	messages.push_back(build_add(102, 'S', 20, 1990000));
	messages.push_back(build_add(103, 'S', 20, 2010000));

	// ================= ASK-SIDE SYMMETRY TESTS =========================
	// Exercises FindWorstIndex's ask branch, which was fixed from
	// "price < worst.price" to "price > worst.price" -- msgs 39-41
	// alone never filled the ask side, so this is the first test that
	// actually runs that code path.

	// --- 42-70: top up asks from 3 to 32 --------------------------------
	// Prices strictly DECREASING as id increases -> id104 is the loop's
	// worst (highest price), id132 its best (lowest price). Room for
	// exactly 29 more (3+29=32) -- this loop fills the side WITHOUT
	// triggering eviction. Note: id103 (added earlier, @2,010,000) is
	// still the book's true worst after this loop, not anything from
	// the loop itself -- id101/102/103 predate it and are untouched.
	for (uint64_t id = 104; id <= 132; id++) {
		uint32_t price = 1980000 - uint32_t(id - 104) * 1000;
		messages.push_back(build_add(id, 'S', 10, price));
	}

	// --- 71: Add id400, price lower than everyone -> expect EVICTED ----
	// Victim should be id103 (the true worst / highest price on the ask
	// side -- predates the loop above, never displaced by it).
	messages.push_back(build_add(400, 'S', 10, 1900000));

	// --- 72: Add id401, price above the new worst (id101 @ 2,000,000) --
	// Expect DISCARDED (less competitive than current worst ask).
	messages.push_back(build_add(401, 'S', 10, 2050000));

	// --- 73: Add id402, price exactly ties the current worst (id101) ---
	// Expect DISCARDED (FCFS: id101 keeps its place on a tie).
	messages.push_back(build_add(402, 'S', 10, 2000000));

	// --- 74: Cancel id101 -> frees one ask slot (32 -> 31) --------------
	messages.push_back(build_delete(101));

	// --- 75: Add id403, deliberately uncompetitive price, room free ----
	// Expect INSERTED (competitiveness check only applies when full).
	messages.push_back(build_add(403, 'S', 5, 2100000));

	// --- 76: Execute id403, full fill (qty 5 -> 0) ----------------------
	// Confirmed via ExecuteOrder's swap-and-pop: slot frees, ask_count
	// drops 32 -> 31.
	messages.push_back(build_execute(403, 5, /*matchId=*/9002));

	// --- 77: Add id600 on the BID side -> independence check -----------
	// Bids are already full at 32 (unaffected by anything above), so
	// this should trigger a BID-side eviction only. Confirms the ask
	// count from msg 76 doesn't get disturbed by unrelated bid activity.
	messages.push_back(build_add(600, 'B', 10, 1060000));

	Bytes file = build_mold_file(messages);

	constexpr const char* outPath = "stress_test.mold";
	std::ofstream out(outPath, std::ios::binary);
	if (!out) {
		std::cerr << "Failed to open " << outPath << " for writing." << std::endl;
		return 1;
	}
	out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
	out.close();

	std::cout << "Wrote " << messages.size() << " messages ("
		<< file.size() << " bytes) to " << outPath << std::endl;
	std::cout << "Expected final book state: bids=32 asks=31" << std::endl;

	return 0;
}