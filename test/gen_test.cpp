/*
   gen_test.cpp
   ----------------
   Standalone MoldUDP64 test-feed generator (own main(), separate CMake
   target). Writes stress_test.mold: one deterministic sequence that walks
   every book code path for all seven message types. Run the model on it
   with --base-price 1000000 ($100.00) --symbol STRESS: the window is then
   [$18.08, $263.83] in $0.01 ticks.

   Framing, like a real feed: packets of at most PACKET_MSGS messages with
   continuous sequence numbers from 1, a heartbeat after the second packet,
   and an end-of-session packet last. Also writes stress_gap.mold, the same
   feed with one packet's sequence number skipped ahead -- the model must
   flag mold_gap_seen from there on.

   Messages the book must SKIP, mixed in (8 of them):
     S  system event 'O' first and 'C' last            (locate 0)
     R  stock directory for STRESS (locate 1) and OTHER (locate 2)
     N  retail price improvement indicator for STRESS
     P  hidden-order trade for STRESS, 44 bytes -- looks like an execution,
        must not touch the book
     A  two OTHER adds (locate 2) at prices that would beat STRESS's final
        BBO if --symbol failed to filter them
   Every book message below is for STRESS (locate 1).

   Message plan (prices in ITCH units, 10000 = $1):
     Book build
       bids  1 @ 990000 x10, 2 @ 990000 x10, 3 @ 980000 x10,
             4 @ 970000 x10, 5 @ 995000 x10          (A)
       asks 11 @1005000 x10, 12 @1005000 x10,
            13 @1010000 x10, 14 @1020000 x10         (F: MPID adds)
     Reduce / delete / replace
       E  1 x4                    partial      -> 990000: 16 sh, 2 orders
       C 11 x3, trade px garbage  partial      -> book uses 11's own level
       X 12 x5                    partial      -> 1005000: 12 sh, 2 orders
       X 13 x10                   full cancel  -> removed
       D  5                       best bid gone -> rescan to 990000
       U  3 -> 21 @ 999000 x25    new best bid, side inherited (bid)
       U 14 -> 22 @1001000 x15    new best ask, side inherited (ask)
     Price window                                     price_rejected
       A 30 @1000050  off-tick                         1
       A 31 @2638400  one tick above the window        2
       A 32 @ 180700  one tick below the window        3
       U  4 -> 33 @ 970050 off-tick: 4 removed         4
     Hash table: 19 bids in set 100, x1 @ 900000      dropped
       k = 0..18    fill 3 ways + 16 CAM
       k = 19, 20   set + CAM full                     1, 2
       D k=1        frees way 1;    A k=21 -> way 1
       D k=10       frees CAM 7;    A k=22 -> CAM 7
       A k=23       full again                         3
     Unknown refs                                     unknown_order_ref
       D 30         price-rejected, never stored       1
       E k=19       dropped, never stored              2
       X 13         already fully cancelled            3
       U  4 -> 40   removed by the off-tick replace    4
       C 9999       never existed                      5
     Ask wipeout and recovery
       D 22; E 11 x7 (fills it); D 12   -> asks empty
       F 50 @1002000 x8                  -> recovery

   Expected: 63 messages, 55 applied, 8 skipped, 0 sequence gaps.
   Expected final BBO: bid 999000 x 25 (1 order), ask 1002000 x 8 (1 order).
   Expected counters: dropped=3 price_rejected=4 unknown_order_ref=5.
   CMake's `stress` test checks this against Orderbook-cpp's output.
*/

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <cassert>
#include <vector>
#include <fstream>
#include <iostream>

#include "../include/itchparser.h"   // message lengths: the generator and
                                     // the parser share one set of constants
#include "../include/ordertable.h"   // HashOrn: colliding ORNs are built
                                     // with the table's own hash

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

/* Stock locates: STRESS is the symbol under test, OTHER must be filtered. */
constexpr uint16_t LOCATE_SYSTEM = 0;
constexpr uint16_t LOCATE_STRESS = 1;
constexpr uint16_t LOCATE_OTHER = 2;

/* Type byte, stock locate, then the 8 header bytes the book ignores
   (tracking number, timestamp). Book messages default to STRESS. */
static Bytes start(char type, uint16_t locate = LOCATE_STRESS) {
	Bytes m;
	m.push_back(uint8_t(type));
	write_u16_be(m, locate);
	for (std::size_t i = 0; i < 8; i++) m.push_back(0);
	return m;
}

/* build_add / build_add_mpid
   [11..18] orderId, [19] side, [20..23] shares, [24..31] stock (unused),
   [32..35] price; 'F' adds a 4-byte attribution at [36..39].
*/
static Bytes build_add_common(char type, uint64_t orderId, char side, uint32_t qty, uint32_t price,
	uint16_t locate = LOCATE_STRESS) {
	Bytes m = start(type, locate);
	write_u64_be(m, orderId);
	m.push_back(uint8_t(side));
	write_u32_be(m, qty);
	for (std::size_t i = 0; i < 8; i++) m.push_back(0);
	write_u32_be(m, price);
	return m;
}

static Bytes build_add(uint64_t orderId, char side, uint32_t qty, uint32_t price,
	uint16_t locate = LOCATE_STRESS) {
	Bytes m = build_add_common('A', orderId, side, qty, price, locate);
	assert(m.size() == ADD_ORDER_LEN);
	return m;
}

static Bytes build_add_mpid(uint64_t orderId, char side, uint32_t qty, uint32_t price) {
	Bytes m = build_add_common('F', orderId, side, qty, price);
	for (char c : {'T', 'E', 'S', 'T'}) m.push_back(uint8_t(c));
	assert(m.size() == ADD_ORDER_MPID_LEN);
	return m;
}

/* build_delete: [11..18] orderId */
static Bytes build_delete(uint64_t orderId) {
	Bytes m = start('D');
	write_u64_be(m, orderId);
	assert(m.size() == ORDER_DELETE_LEN);
	return m;
}

/* build_replace: [11..18] old id, [19..26] new id, [27..30] shares, [31..34] price */
static Bytes build_replace(uint64_t oldId, uint64_t newId, uint32_t qty, uint32_t price) {
	Bytes m = start('U');
	write_u64_be(m, oldId);
	write_u64_be(m, newId);
	write_u32_be(m, qty);
	write_u32_be(m, price);
	assert(m.size() == ORDER_REPLACE_LEN);
	return m;
}

/* build_execute: [11..18] orderId, [19..22] executed, [23..30] match id */
static Bytes build_execute(uint64_t orderId, uint32_t execQty, uint64_t matchId) {
	Bytes m = start('E');
	write_u64_be(m, orderId);
	write_u32_be(m, execQty);
	write_u64_be(m, matchId);
	assert(m.size() == ORDER_EXECUTE_LEN);
	return m;
}

/* build_execute_price: 'E' layout + [31] printable, [32..35] trade price */
static Bytes build_execute_price(uint64_t orderId, uint32_t execQty, uint64_t matchId, uint32_t tradePrice) {
	Bytes m = start('C');
	write_u64_be(m, orderId);
	write_u32_be(m, execQty);
	write_u64_be(m, matchId);
	m.push_back('Y');
	write_u32_be(m, tradePrice);
	assert(m.size() == ORDER_EXECUTE_PRICE_LEN);
	return m;
}

/* build_cancel: [11..18] orderId, [19..22] cancelled shares */
static Bytes build_cancel(uint64_t orderId, uint32_t qty) {
	Bytes m = start('X');
	write_u64_be(m, orderId);
	write_u32_be(m, qty);
	assert(m.size() == ORDER_CANCEL_LEN);
	return m;
}

/* Non-book messages (ITCH 5.0 layouts; only the length and the type /
   locate bytes matter to the model, which must skip all of them). */
static Bytes pad_to(Bytes m, std::size_t len) {
	while (m.size() < len) m.push_back(0);
	assert(m.size() == len);
	return m;
}

static Bytes build_system_event(char code) {   // 'S', 12 bytes
	Bytes m = start('S', LOCATE_SYSTEM);
	m.push_back(uint8_t(code));
	return pad_to(m, 12);
}

static Bytes build_stock_directory(uint16_t locate, const char* symbol) {   // 'R', 39 bytes
	Bytes m = start('R', locate);
	const std::size_t n = std::strlen(symbol);
	assert(n <= 8);
	for (std::size_t i = 0; i < 8; i++)
		m.push_back(uint8_t(i < n ? symbol[i] : ' '));   // space-padded to 8
	return pad_to(m, 39);
}

static Bytes build_rpii() {   // 'N', 20 bytes
	Bytes m = start('N');
	for (char c : {'S', 'T', 'R', 'E', 'S', 'S', ' ', ' '}) m.push_back(uint8_t(c));
	m.push_back('B');
	return pad_to(m, 20);
}

static Bytes build_hidden_trade(uint64_t orderId, uint32_t qty, uint32_t price) {   // 'P', 44 bytes
	Bytes m = start('P');
	write_u64_be(m, orderId);        // a resting order's id: must still not reduce it
	m.push_back('B');
	write_u32_be(m, qty);
	for (std::size_t i = 0; i < 8; i++) m.push_back(' ');
	write_u32_be(m, price);
	write_u64_be(m, 9999);
	return pad_to(m, 44);
}

/* MoldUDP64 framing (header: session 10, sequence 8, count 2). */
static void write_mold_header(Bytes& buf, uint64_t sequence, uint16_t count) {
	for (std::size_t i = 0; i < 10; i++) buf.push_back(uint8_t("STRESSTEST"[i]));
	write_u64_be(buf, sequence);
	write_u16_be(buf, count);
}

constexpr std::size_t PACKET_MSGS = 16;

/* build_mold_stream
   Packets of at most PACKET_MSGS messages back to back, sequence numbers
   from 1, a heartbeat after the second packet, end-of-session last.
   gap_after: skip the sequence number ahead by one at that packet (0 = no
   gap).
*/
static Bytes build_mold_stream(const std::vector<Bytes>& messages, std::size_t gap_after) {
	Bytes buf;
	uint64_t seq = 1;
	std::size_t packet = 0;
	for (std::size_t i = 0; i < messages.size(); i += PACKET_MSGS, packet++) {
		const std::size_t n = std::min(PACKET_MSGS, messages.size() - i);
		assert(n <= 0xFFFE);   // 0xFFFF is end-of-session
		if (gap_after && packet == gap_after) seq++;
		write_mold_header(buf, seq, uint16_t(n));
		for (std::size_t k = i; k < i + n; k++) {
			write_u16_be(buf, uint16_t(messages[k].size()));
			buf.insert(buf.end(), messages[k].begin(), messages[k].end());
		}
		seq += n;
		if (packet == 1) write_mold_header(buf, seq, 0x0000);   // heartbeat
	}
	write_mold_header(buf, seq, 0xFFFF);                        // end of session
	return buf;
}

static bool write_file(const char* path, const Bytes& data) {
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		std::cerr << "Failed to open " << path << " for writing." << std::endl;
		return false;
	}
	out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
	return bool(out);
}

/* An ORN in `set`, distinct for each k < SETS: the chunk at bits
   [HASH_W, 2*HASH_W) cancels the k mixed into the low chunk. */
static uint64_t OrnInSet(uint32_t set, uint32_t k) {
	const uint64_t orn = (uint64_t(k) << HASH_W) | (set ^ k);
	assert(HashOrn(orn) == set);
	return orn;
}

int main() {
	std::vector<Bytes> m;

	// ---- Start of day: system event, stock directory (skipped) ----------
	m.push_back(build_system_event('O'));
	m.push_back(build_stock_directory(LOCATE_STRESS, "STRESS"));
	m.push_back(build_stock_directory(LOCATE_OTHER, "OTHER"));

	// ---- Book build ----------------------------------------------------
	m.push_back(build_add(1, 'B', 10, 990000));
	m.push_back(build_add(2, 'B', 10, 990000));
	m.push_back(build_add(3, 'B', 10, 980000));
	m.push_back(build_add(4, 'B', 10, 970000));
	m.push_back(build_add(5, 'B', 10, 995000));
	m.push_back(build_add_mpid(11, 'S', 10, 1005000));
	m.push_back(build_add_mpid(12, 'S', 10, 1005000));
	m.push_back(build_add_mpid(13, 'S', 10, 1010000));
	m.push_back(build_add_mpid(14, 'S', 10, 1020000));
	m.push_back(build_rpii());                               // skipped
	m.push_back(build_add(900001, 'B', 10, 999900, LOCATE_OTHER));   // skipped: OTHER

	// ---- Reduce / delete / replace ---------------------------------------
	m.push_back(build_hidden_trade(1, 4, 990000));          // skipped: must NOT reduce id 1
	m.push_back(build_execute(1, 4, /*matchId=*/9001));
	m.push_back(build_execute_price(11, 3, /*matchId=*/9002, /*tradePrice=*/0xFFFFFFFF));
	m.push_back(build_cancel(12, 5));
	m.push_back(build_cancel(13, 10));
	m.push_back(build_delete(5));
	m.push_back(build_replace(3, 21, 25, 999000));
	m.push_back(build_replace(14, 22, 15, 1001000));

	// ---- Price window ------------------------------------------------------
	m.push_back(build_add(30, 'B', 10, 1000050));     // off-tick
	m.push_back(build_add(31, 'S', 10, 2638400));     // one tick above
	m.push_back(build_add(32, 'B', 10, 180700));      // one tick below
	m.push_back(build_replace(4, 33, 10, 970050));    // off-tick: 4 removed

	// ---- Hash table: fill set 100's ways and the whole CAM -----------------
	constexpr uint32_t SET = 100;
	for (uint32_t k = 0; k < WAYS + CAM_DEPTH; k++)
		m.push_back(build_add(OrnInSet(SET, k), 'B', 1, 900000));
	m.push_back(build_add(OrnInSet(SET, 19), 'B', 1, 900000));   // dropped
	m.push_back(build_add(OrnInSet(SET, 20), 'B', 1, 900000));   // dropped
	m.push_back(build_delete(OrnInSet(SET, 1)));                  // frees way 1
	m.push_back(build_add(OrnInSet(SET, 21), 'B', 1, 900000));   // -> way 1
	m.push_back(build_delete(OrnInSet(SET, 10)));                 // frees CAM 7
	m.push_back(build_add(OrnInSet(SET, 22), 'B', 1, 900000));   // -> CAM 7
	m.push_back(build_add(OrnInSet(SET, 23), 'B', 1, 900000));   // dropped

	// ---- Unknown refs --------------------------------------------------------
	m.push_back(build_delete(30));
	m.push_back(build_execute(OrnInSet(SET, 19), 1, /*matchId=*/9100));
	m.push_back(build_cancel(13, 1));
	m.push_back(build_replace(4, 40, 10, 970000));
	m.push_back(build_execute_price(9999, 1, /*matchId=*/9101, 1000000));

	// ---- Ask wipeout and recovery ----------------------------------------------
	m.push_back(build_delete(22));
	m.push_back(build_execute(11, 7, /*matchId=*/9200));
	m.push_back(build_delete(12));
	m.push_back(build_add_mpid(50, 'S', 8, 1002000));
	m.push_back(build_add(900002, 'S', 10, 1000100, LOCATE_OTHER));  // skipped: OTHER
	m.push_back(build_system_event('C'));                    // skipped

	const Bytes feed = build_mold_stream(m, 0);
	const Bytes gapped = build_mold_stream(m, /*gap_after=*/2);
	if (!write_file("stress_test.mold", feed) || !write_file("stress_gap.mold", gapped))
		return 1;

	std::cout << "Wrote " << m.size() << " messages (" << feed.size()
		<< " bytes) to stress_test.mold and stress_gap.mold" << std::endl;
	std::cout << "Expected: 55 applied, 8 skipped; bid 999000 x 25 (1), ask 1002000 x 8 (1); "
		"dropped=3 price_rejected=4 unknown_order_ref=5" << std::endl;
	return 0;
}
