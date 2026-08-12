/*

itchparser.h
------------

Small, well-documented helpers to parse fixed-format ITCH-style
market data messages from a raw byte buffer. The parser exposes
low-level big-endian readers and higher-level message parsers.

Design notes for beginners:
- Wire messages are fixed-length and encoded in big-endian order.
- The read_u*_be helpers read integers from a buffer at a given
  offset but do NO bounds checking. This keeps them fast and
  simple; callers must ensure enough bytes are available before
  calling them.
- The parse_* helpers DO perform length checks and return
  std::nullopt when the buffer does not contain a complete
  message at the requested offset.

*/


#pragma once

#include <cstdint>
#include <optional>

#include "types.h"

/* Fixed-offset big-endian readers — no bounds checking.

Parameters:
- buf: pointer to the start of the byte buffer (raw network bytes)
- offset: zero-based byte index inside buf where the integer begins

Returns the integer value interpreted as unsigned in host byte order. */

uint16_t read_u16_be(const uint8_t* buf, size_t offset);
uint32_t read_u32_be(const uint8_t* buf, size_t offset);
uint64_t read_u64_be(const uint8_t* buf, size_t offset);


/* Message-type total lengths (bytes). These constants describe the
total size of each message on the wire. Parsers compare remaining
buffer length against these values to decide if a whole message is
available. */

static constexpr size_t ADD_ORDER_LEN      = 36;
static constexpr size_t ORDER_DELETE_LEN   = 19;
static constexpr size_t ORDER_REPLACE_LEN  = 35;
static constexpr size_t ORDER_EXECUTE_LEN = 31;

/*	Parses an 'A' (Add Order) message at `offset` in `buf`.
	Parameters:
	- buf: pointer to the raw message buffer
	- buf_len: total length of the buffer (number of valid bytes)
	- offset: zero-based index inside buf where this message starts

	Returns: std::optional<Order>
	- If the buffer does not contain ADD_ORDER_LEN bytes from offset
	  onward, returns std::nullopt to indicate the message is incomplete.
	- On success, returns an Order populated with fields decoded from
	  the wire format. The Order type is defined in include/types.h. */

std::optional<Order> parse_add(const uint8_t* buf, size_t buf_len, size_t offset);


/* Parses a 'D' (Order Delete) message at `offset` in `buf`.
Returns the Order Reference Number to delete, or std::nullopt if the
buffer doesn't have ORDER_DELETE_LEN bytes remaining from offset.
Note: the returned value is the 64-bit order reference from the wire. */
std::optional<uint64_t> parse_delete(const uint8_t* buf, size_t buf_len, size_t offset);


/* Parses a 'U' (Order Replace) message at `offset` in `buf`.
The 'U' message on the wire contains new/old order ids and new
price/quantity but does not include the order side (buy/sell).
Therefore parse_replace returns only the raw wire fields so the
caller can look up the existing order to recover the side.

ReplaceFields fields:
- OldOrderId: the order reference being replaced (old id)
- NewOrderId: the new order reference on the wire
- price, quantity: new resting price and quantity */
struct ReplaceFields {
	OrderId OldOrderId;
	OrderId NewOrderId;
	Price   price;
	Quantity quantity;
	/* Side is intentionally omitted because the wire 'U' message
	does not contain it. Callers must look up the existing order
	by OldOrderId to obtain the side before applying the replace. */
};
std::optional<ReplaceFields> parse_replace(const uint8_t* buf, size_t buf_len, size_t offset);


/* Parses an 'E' (Execute) message at `offset` in `buf`.
The OrderExecute struct (defined in include/types.h) contains the
decoded fields: orderId, executedQuantity, and matchId. Returns
std::nullopt when there are not enough bytes for a complete message. */
std::optional<OrderExecute> parse_execute(const uint8_t* buf, size_t buf_len, size_t offset);
