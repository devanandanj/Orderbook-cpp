/*
   test_command.cpp
   ITCH bytes -> Command for every supported type, and Command -> status
   bits / trace reason for every book outcome.
*/

#include <cassert>
#include <cstdio>
#include <cstring>
#include "../include/command.h"
#include "../include/itchparser.h"
#include "../include/snapshot.h"

static constexpr Price BASE = 1500000;
static Book book;

static void put_be(uint8_t* p, uint64_t v, const size_t width) {
	for (size_t i = width; i-- > 0; v >>= 8) p[i] = static_cast<uint8_t>(v);
}

static void test_parse_all_types() {
	uint8_t a[ADD_ORDER_MPID_LEN] = {};
	a[0] = 'A'; put_be(a + 11, 77, 8); a[19] = 'S'; put_be(a + 20, 300, 4); put_be(a + 32, 1500100, 4);
	auto c = ParseCommand(a, ADD_ORDER_LEN);
	assert(c && c->type == 'A' && c->side == Side::Sell && c->orderId == 77 && c->shares == 300 && c->price == 1500100);

	a[0] = 'F';
	c = ParseCommand(a, ADD_ORDER_MPID_LEN);
	assert(c && c->type == 'F' && c->orderId == 77 && c->price == 1500100);
	assert(!ParseCommand(a, ADD_ORDER_LEN));                 // 'F' needs 40 bytes

	uint8_t e[ORDER_EXECUTE_PRICE_LEN] = {};
	e[0] = 'E'; put_be(e + 11, 5, 8); put_be(e + 19, 40, 4);
	c = ParseCommand(e, ORDER_EXECUTE_LEN);
	assert(c && c->type == 'E' && c->orderId == 5 && c->shares == 40);
	e[0] = 'C'; put_be(e + 32, 0xFFFFFFFF, 4);
	c = ParseCommand(e, ORDER_EXECUTE_PRICE_LEN);
	assert(c && c->type == 'C' && c->shares == 40 && c->price == 0);   // trade price not carried

	uint8_t x[ORDER_CANCEL_LEN] = {};
	x[0] = 'X'; put_be(x + 11, 6, 8); put_be(x + 19, 9, 4);
	c = ParseCommand(x, ORDER_CANCEL_LEN);
	assert(c && c->type == 'X' && c->orderId == 6 && c->shares == 9);

	uint8_t d[ORDER_DELETE_LEN] = {};
	d[0] = 'D'; put_be(d + 11, 8, 8);
	c = ParseCommand(d, ORDER_DELETE_LEN);
	assert(c && c->type == 'D' && c->orderId == 8);

	uint8_t u[ORDER_REPLACE_LEN] = {};
	u[0] = 'U'; put_be(u + 11, 8, 8); put_be(u + 19, 9, 8); put_be(u + 27, 12, 4); put_be(u + 31, 1500200, 4);
	c = ParseCommand(u, ORDER_REPLACE_LEN);
	assert(c && c->type == 'U' && c->orderId == 8 && c->newOrderId == 9 && c->shares == 12 && c->price == 1500200);

	// Unsupported types (system event, stock directory, ...) and empty input.
	uint8_t s[ADD_ORDER_LEN] = {};
	s[0] = 'S';
	assert(!ParseCommand(s, sizeof s));
	assert(!ParseCommand(s, 0));
	printf("parse_all_types: PASS\n");
}

static Command Add(const OrderId id, const Price p) { return Command{'A', Side::Buy, id, 0, 10, p}; }

static void test_outcomes() {
	constexpr uint8_t DROPPED = 1u << SNAP_STATUS_ORDER_DROPPED;
	constexpr uint8_t PRICE = 1u << SNAP_STATUS_PRICE_REJECTED;
	constexpr uint8_t UNKNOWN = 1u << SNAP_STATUS_UNKNOWN_ORDER_REF;

	ResetBook(&book, BASE);
	Outcome o = ApplyCommand(&book, Add(1, BASE));
	assert(o.status == 0 && o.accepted && !o.reason);

	o = ApplyCommand(&book, Add(2, BASE + 1));
	assert(o.status == PRICE && !o.accepted && std::strcmp(o.reason, "price_rejected") == 0);

	// Fill set 3's ways and the CAM, then one more.
	for (uint32_t k = 0; k < WAYS + CAM_DEPTH; k++)
		ApplyCommand(&book, Add((OrderId(k) << HASH_W) | (3 ^ k), BASE));
	o = ApplyCommand(&book, Add((OrderId(99) << HASH_W) | (3 ^ 99), BASE));
	assert(o.status == DROPPED && std::strcmp(o.reason, "dropped_table_full") == 0);

	for (const char t : {'E', 'C', 'X', 'D'}) {
		o = ApplyCommand(&book, Command{t, Side::Buy, 12345, 0, 1, 0});
		assert(o.status == UNKNOWN && std::strcmp(o.reason, "not_found") == 0);
	}
	o = ApplyCommand(&book, Command{'U', Side::Buy, 12345, 6, 1, BASE});
	assert(o.status == UNKNOWN && std::strcmp(o.reason, "old_id_not_found") == 0);
	o = ApplyCommand(&book, Command{'U', Side::Buy, 1, 6, 1, BASE + 1});
	assert(o.status == PRICE);
	printf("outcomes: PASS\n");
}

int main() {
	test_parse_all_types();
	test_outcomes();

	printf("\nAll command tests PASSED\n");
	return 0;
}
