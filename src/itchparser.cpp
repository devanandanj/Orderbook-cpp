#include "../include/itchparser.h"

uint16_t read_u16_be(const uint8_t* buf, size_t offset) {
	return (uint16_t(buf[offset]) << 8) | buf[offset + 1];
}

uint32_t read_u32_be(const uint8_t* buf, size_t offset) {
	return (uint32_t(read_u16_be(buf, offset)) << 16) | read_u16_be(buf, offset + 2);
}

uint64_t read_u64_be(const uint8_t* buf, size_t offset) {
	return (uint64_t(read_u32_be(buf, offset)) << 32) | read_u32_be(buf, offset + 4);
}

//parse add
std::optional<Order> parse_add(const uint8_t* buf, size_t buf_len, size_t offset) {
	if (offset + ADD_ORDER_LEN > buf_len)
	{
		return std::nullopt;
	}

	// Message Type at offset+0 ('A') — not read here; dispatch loop
	// in main.cpp is responsible for routing based on this byte
	// before calling parse_add at all

	Order order;
	order.orderId = read_u64_be(buf, offset + 11);
	order.side = (buf[offset + 19] == 'B') ? Side::Buy : Side::Sell;
	order.price = read_u32_be(buf, offset + 32);
	order.quantity = read_u32_be(buf, offset + 20);
	return order;
}

//parse delete
std::optional<uint64_t> parse_delete(const uint8_t* buf, size_t buf_len, size_t offset) {
	if (offset + ORDER_DELETE_LEN > buf_len)
	{
		return std::nullopt;
	}
	return read_u64_be(buf, offset + 11);
}

//parse replace
std::optional<ReplaceFields> parse_replace(const uint8_t* buf, size_t buf_len, size_t offset) {
	if (offset + ORDER_REPLACE_LEN > buf_len)
	{
		return std::nullopt;
	}

	ReplaceFields fields;
	fields.OldOrderId	= read_u64_be(buf, offset + 11);
	fields.NewOrderId	= read_u64_be(buf, offset + 19);
	fields.price		= read_u32_be(buf, offset + 31);
	fields.quantity		= read_u32_be(buf, offset + 27);
	return fields;

}

//Execute Order
std::optional<OrderExecute> parse_execute(const uint8_t* buf, size_t buf_len, size_t offset) {
	if (buf_len < offset + 31)
	{
		return std::nullopt;
	}

	const uint8_t* p = buf + offset;

	if (p[0] != 'E')
	{
		return std::nullopt;
	}
	OrderExecute exec;
	exec.orderId = read_u64_be(buf, offset + 11);
	exec.executedQuantity = read_u32_be(buf, offset + 19);
	exec.matchId = read_u64_be(buf, offset + 23);

	return exec;
}