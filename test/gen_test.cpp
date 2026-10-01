/*
   gen_test.cpp
   ----------------
   Standalone MoldUDP64 test-feed generator (own main(), separate CMake
   target). Writes stress_test.mold: one deterministic sequence that
   walks every book code path, symmetrically on bids and asks, scaled to
   CAP = MAX_ORDERS_PER_SIDE so the eviction boundary is always hit
   whatever the capacity is.

   Message plan:
     Bids
       fill     ids 1..CAP, strictly increasing price (id1 worst)
       EVICT    id1001 best price             -> evicts id1
       DISCARD  id1002 below worst (id2)      -> bid reject 1
       TIE      id1003 equals worst (id2)     -> bid reject 2 (FCFS)
       cancel   id2                           -> room
       INSERT   id1004 very low price         -> room, so inserted
       exec     id1004 partial (5 -> 3)
       replace  id1004 -> id200               -> Replaced
     Asks
       add      id101, id102, id103 (id103 highest = worst)
       fill     ids 104..CAP+100, decreasing price -> side full
       EVICT    id400 best price              -> evicts id103
       DISCARD  id401 above worst (id101)     -> ask reject 1
       TIE      id402 equals worst (id101)    -> ask reject 2 (FCFS)
       cancel   id101                         -> room
       INSERT   id403 uncompetitive           -> room, so inserted
       exec     id403 full fill               -> removed
     Cross-side
       EVICT    id600 bid, beats worst bid (id3) -> evicts id3
     Wipeouts
       cancel every bid (4..CAP, 1001, 200, 600)   -> bids=0
       add id700 bid                                -> recovery
       full-fill every ask (102, 104..CAP+100, 400) -> asks=0
       add id800 ask                                -> recovery
     Unknown refs (bounded book: the feed still references orders the
     model evicted, discarded or already filled -- flagged, not fatal)
       delete  id1    (evicted)
       execute id1002 (discarded)
       replace id403 -> id900 (filled)

   Expected: final bids=1 asks=1, rejects bid=2 ask=2, unknown_order_ref=3.
   CMake's `stress` test checks this against Orderbook-cpp's output.
*/

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <vector>
#include <fstream>
#include <iostream>

#include "../include/using.h"
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
	constexpr uint64_t CAP = MAX_ORDERS_PER_SIDE;
	std::vector<Bytes> messages;

	// ---- Bids --------------------------------------------------------
	for (uint64_t id = 1; id <= CAP; id++)
		messages.push_back(build_add(id, 'B', 10, 1000000 + uint32_t(id - 1) * 1000));
	messages.push_back(build_add(1001, 'B', 10, 1500000));   // EVICT id1
	messages.push_back(build_add(1002, 'B', 10, 900000));    // DISCARD
	messages.push_back(build_add(1003, 'B', 10, 1001000));   // TIE id2 -> DISCARD
	messages.push_back(build_delete(2));
	messages.push_back(build_add(1004, 'B', 5, 800000));     // room -> INSERT
	messages.push_back(build_execute(1004, 2, /*matchId=*/9001));
	messages.push_back(build_replace(1004, 200, 50, 1200000));

	// ---- Asks --------------------------------------------------------
	messages.push_back(build_add(101, 'S', 20, 2000000));
	messages.push_back(build_add(102, 'S', 20, 1990000));
	messages.push_back(build_add(103, 'S', 20, 2010000));
	for (uint64_t id = 104; id <= CAP + 100; id++)
		messages.push_back(build_add(id, 'S', 10, 1980000 - uint32_t(id - 104) * 1000));
	messages.push_back(build_add(400, 'S', 10, 1500000));    // EVICT id103
	messages.push_back(build_add(401, 'S', 10, 2050000));    // DISCARD
	messages.push_back(build_add(402, 'S', 10, 2000000));    // TIE id101 -> DISCARD
	messages.push_back(build_delete(101));
	messages.push_back(build_add(403, 'S', 5, 2100000));     // room -> INSERT
	messages.push_back(build_execute(403, 5, /*matchId=*/9002));

	// ---- Cross-side: bids still full, id600 evicts id3 ---------------
	messages.push_back(build_add(600, 'B', 10, 1060000));

	// ---- Wipeouts ----------------------------------------------------
	for (uint64_t id = 4; id <= CAP; id++)
		messages.push_back(build_delete(id));
	messages.push_back(build_delete(1001));
	messages.push_back(build_delete(200));
	messages.push_back(build_delete(600));
	messages.push_back(build_add(700, 'B', 10, 1000000));

	messages.push_back(build_execute(102, 20, /*matchId=*/9101));
	for (uint64_t id = 104; id <= CAP + 100; id++)
		messages.push_back(build_execute(id, 10, /*matchId=*/9200 + id));
	messages.push_back(build_execute(400, 10, /*matchId=*/9102));
	messages.push_back(build_add(800, 'S', 10, 2000000));

	// ---- Unknown refs ------------------------------------------------
	messages.push_back(build_delete(1));
	messages.push_back(build_execute(1002, 1, /*matchId=*/9300));
	messages.push_back(build_replace(403, 900, 10, 2000000));

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
	std::cout << "Expected: bids=1 asks=1, rejects bid=2 ask=2 unknown_order_ref=3" << std::endl;
	return 0;
}
