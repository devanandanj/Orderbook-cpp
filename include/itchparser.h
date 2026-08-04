#pragma once 

#include <cstdint>
#include <optional>

#include "types.h"


// Fixed-offset big-endian readers — no bounds checking.
// Callers (parse_* functions) are responsible for validating buffer length first.

uint16_t read_u16_be(const uint8_t* buf, size_t offset);
uint32_t read_u32_be(const uint8_t* buf, size_t offset);
uint64_t read_u64_be(const uint8_t* buf, size_t offset);


// Message-type total lengths.
static constexpr size_t ADD_ORDER_LEN		= 36;
static constexpr size_t ORDER_DELETE_LEN	= 19;
static constexpr size_t ORDER_REPLACE_LEN	= 35;


// Parses an 'A' (Add Order) message at `offset` in `buf`.
// Returns std::nullopt if buf doesn't have ADD_ORDER_LEN bytes remaining from offset.
std::optional<Order> parse_add(const uint8_t* buf, size_t buf_len, size_t offset);


// Parses a 'D' (Order Delete) message at `offset` in `buf`.
// Returns the Order Reference Number to delete, or std::nullopt if buf doesn't have ORDER_DELETE_LEN bytes remaining.
std::optional<uint64_t> parse_delete(const uint8_t* buf, size_t buf_len, size_t offset);


// Parses a 'U' (Order Replace) message at `offset` in `buf`.
// Returns raw wire fields only — side is NOT included (spec: not present on 'U' messages).
// Caller is responsible for looking up existing order by oldOrderId to recover side
// before constructing a full Order/OrderModify for the orderbook.
struct ReplaceFields {
	OrderId OldOrderId;
	OrderId NewOrderId;
	Price price;
	Quantity quantity;
	//caller must look side up - intentionally left out
};

std::optional<ReplaceFields> parse_replace(const uint8_t* buf, size_t buf_len, size_t offset);