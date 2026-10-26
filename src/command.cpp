/*
   command.cpp
   ----------------
   ITCH message -> Command, and Command -> book operation. See
   include/command.h.

*/

#include "../include/command.h"
#include "../include/itchparser.h"
#include "../include/snapshot.h"

static Command FromAdd(const char type, const Order& o) {
	return Command{type, o.side, o.orderId, 0, o.quantity, o.price};
}

static Command FromReduce(const char type, const OrderId orderId, const Quantity shares) {
	return Command{type, Side::Buy, orderId, 0, shares, 0};
}

std::optional<Command> ParseCommand(const uint8_t* data, const size_t length) {
	if (length == 0) return std::nullopt;

	switch (data[0]) {
	case 'A':
		if (auto o = parse_add(data, length, 0)) return FromAdd('A', *o);
		break;
	case 'F':
		if (auto o = parse_add_mpid(data, length, 0)) return FromAdd('F', *o);
		break;
	case 'E':
		if (auto e = parse_execute(data, length, 0)) return FromReduce('E', e->orderId, e->executedQuantity);
		break;
	case 'C':
		if (auto e = parse_execute_price(data, length, 0)) return FromReduce('C', e->orderId, e->executedQuantity);
		break;
	case 'X':
		if (auto x = parse_cancel(data, length, 0)) return FromReduce('X', x->orderId, x->cancelledQuantity);
		break;
	case 'D':
		if (auto id = parse_delete(data, length, 0)) return FromReduce('D', *id, 0);
		break;
	case 'U':
		if (auto u = parse_replace(data, length, 0))
			return Command{'U', Side::Buy, u->OldOrderId, u->NewOrderId, u->quantity, u->price};
		break;
	default:
		break;
	}
	return std::nullopt;
}

static constexpr uint8_t Bit(const uint8_t pos) { return static_cast<uint8_t>(1u << pos); }

static Outcome FromAdd(const AddResult r) {
	switch (r) {
	case AddResult::Inserted: return {0, true, nullptr};
	case AddResult::Dropped:  return {Bit(SNAP_STATUS_ORDER_DROPPED), false, "dropped_table_full"};
	default:                  return {Bit(SNAP_STATUS_PRICE_REJECTED), false, "price_rejected"};
	}
}

static Outcome FromFound(const bool found) {
	return found ? Outcome{0, true, nullptr}
	             : Outcome{Bit(SNAP_STATUS_UNKNOWN_ORDER_REF), false, "not_found"};
}

Outcome ApplyCommand(Book* book, const Command& cmd) {
	switch (cmd.type) {
	case 'A':
	case 'F':
		return FromAdd(AddOrder(book, Order{cmd.orderId, cmd.side, cmd.price, cmd.shares}));
	case 'E':
	case 'C':
	case 'X':
		return FromFound(ReduceOrder(book, cmd.orderId, cmd.shares));
	case 'D':
		return FromFound(DeleteOrder(book, cmd.orderId));
	case 'U':
		switch (ReplaceOrder(book, OrderModify{cmd.orderId, cmd.newOrderId, cmd.price, cmd.shares})) {
		case ReplaceResult::Replaced: return {0, true, nullptr};
		case ReplaceResult::NotFound: return {Bit(SNAP_STATUS_UNKNOWN_ORDER_REF), false, "old_id_not_found"};
		case ReplaceResult::Dropped:  return {Bit(SNAP_STATUS_ORDER_DROPPED), false, "dropped_table_full"};
		default:                      return {Bit(SNAP_STATUS_PRICE_REJECTED), false, "price_rejected"};
		}
	default:
		BeginMessage(book);
		return {0, false, "unsupported_type"};
	}
}
