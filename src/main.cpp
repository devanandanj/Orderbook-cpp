
/*
   main.cpp
   ----------------
   Reads a MoldUDP64-framed file, deframes the
   envelope into inner messages, parses each message and applies it
   against an in-memory Orderbook.

*/

#include <iostream>
#include <fstream>

#include "../include/using.h"
#include "../include/orderbook.h"
#include "../include/moldudp64.h"
#include "../include/itchparser.h"
#include "../include/trace.h"


// Small functions used to improve readability of code in main.
Order MakeOrder(OrderId orderId, Side side, Price price, Quantity quantity) {
	return Order{ orderId, side, price, quantity };
}

OrderModify MakeOrderModify(OrderId OldOrderId, OrderId NewOrderId, Side side, Price price, Quantity quantity) {
	return OrderModify{ OldOrderId, NewOrderId, side, price, quantity };
}

bool IsBuyOrder(const Order& order) {
	return order.side == Side::Buy;
}

/* read_file_bytes
   Read the entire file into a vector<uint8_t>. Returns an empty vector on
   failure. This helper is synchronous and loads the whole file into memory
   which is fine for small test files used by this project.
*/
static std::vector<uint8_t> read_file_bytes(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		return {};
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
	{
		return {};
	}

	return buffer;

}

/* FindOrderSide
   Look up the side (Buy/Sell) for an existing order id by searching
   both the bids and asks arrays. Returns true and sets 'side' when found.
*/
static bool FindOrderSide(const Orderbook& book, OrderId orderId, Side& side) {
	for (uint8_t i = 0; i < book.bid_count; i++)
	{
		if (book.bids[i].orderId == orderId)
		{
			side = Side::Buy;
			return true;
		}
	}
	for (uint8_t i = 0; i < book.ask_count; i++)
	{
		if (book.asks[i].orderId == orderId) {
			side = Side::Sell;
			return true;
		}
	}
	return false;
}

int main(int argc, char** argv) {
	if (argc != 2)
	{
		std::cerr << "Usage: " << argv[0] << " <MoldUDP64_file>" << std::endl;
		return 1;
	}

	std::vector<uint8_t> buf = read_file_bytes(argv[1]);
	if (buf.empty()) {
		std::cerr << "Failed to read file: " << argv[1] << std::endl;
		return 1;
	}

	std::vector<MoldUDPMessage> messages = deframe_moldudp64(buf.data(), buf.size());
	if (messages.empty())
	{
		std::cerr << "De-framing failed or produced no messages -- aborting." << std::endl;
		return 1;
	}

	Orderbook book = {};
	std::ofstream trace("trace.txt");

	for (size_t i = 0; i < messages.size(); i++)
	{
		const MoldUDPMessage& msg = messages[i];
		if (msg.length == 0)
		{
			std::cerr << "Message " << i << ": zero length -- nothing to dispatch" << std::endl;
			return 1;
		}

		uint8_t msg_type = msg.data[0];

		switch (msg_type) {
		case 'A': {
			auto order = parse_add(msg.data, msg.length, 0);
			if (!order.has_value())
			{
				WriteTraceEntry(trace, i, 'A', 0, false, "malformed", book);
				std::cerr << "Message " << i << ": malformed 'A' message -- aborting." << std::endl;
				return 1;
			}
			AddResult result = AddOrder(&book, *order);
			
			const char* reason = nullptr;
			if (result == AddResult::Evicted) reason = "Evicted worst order";
			if (result == AddResult::Discarded) reason = "Book full order not competitive";
			bool accepted = (result != AddResult::Discarded);

			WriteTraceEntry(trace, i, 'A', order->orderId, accepted, reason, book);
			
			if (!accepted)
			{
				std::clog << "Message" << i <<
					": AddOrder failed (book full) for Order ID = "
					<< (unsigned long long)order->orderId << std::endl;
			}
			break;
		}
		case 'D': {
			auto orderId = parse_delete(msg.data, msg.length, 0);
			if (!orderId.has_value())
			{
				WriteTraceEntry(trace, i, 'D', 0, false, "Malformed", book);
				std::cerr << "Message " << i << ": malformed 'D' message -- aborting." << std::endl;
				return 1;
			}
			bool result = CancelOrder(&book, *orderId);
			if (!result)
			{
				WriteTraceEntry(trace, i, 'D', *orderId, result, "not_found", book);
				std::cerr << "Message " << i << ": CancelOrder failed for Order ID = "
					<< (unsigned long long) * orderId << " -- aborting." << std::endl;
				return 1;
			}
			WriteTraceEntry(trace, i, 'D', *orderId, result, nullptr, book);
			break;
		}
		case 'U': {
			auto fields = parse_replace(msg.data, msg.length, 0);
			if (!fields.has_value())
			{	
				WriteTraceEntry(trace, i, 'U', 0, false, "malformed", book);
				std::cerr << "Message " << i << ": malformed 'U' message -- aborting." << std::endl;
				return 1;
			}
			Side side;
			if (!FindOrderSide(book, fields->OldOrderId, side))
			{
				WriteTraceEntry(trace, i, 'U', fields->OldOrderId, false, "old_id_not_found", book);
				std::cerr << "Message " << i << ": Replace references unknown Old Order ID = "
					<< (unsigned long long)fields->OldOrderId << " -- aborting." << std::endl;
				return 1;
			}
			OrderModify mod{ fields->OldOrderId, fields->NewOrderId, side, fields->price, fields->quantity };
			ModifyResult result = ModifyOrder(&book, mod);
			const char* reason = nullptr;
			if (result == ModifyResult::Evicted) reason = "Evicted worst";
			if (result == ModifyResult::Discarded) reason = "Book full - Not Competitive";
			if (result == ModifyResult::NotFound) reason = "Old ID is not found";

			bool accepted = (result == ModifyResult::Replaced || result == ModifyResult::Evicted);
			WriteTraceEntry(trace, i, 'U', fields->NewOrderId, accepted, reason, book);

			if (!accepted)
			{
				WriteTraceEntry(trace, i, 'E', 0, false, "malformed", book);
				std::cerr << "Message " << i
					<< ": ModifyOrder failed (book full after cancel) for New Order ID "
					<< (unsigned long long)fields->NewOrderId << " --aborting" << std::endl;
				return 1;
			}
			break;
		}
		case 'E': {
			auto exec = parse_execute(msg.data, msg.length, 0);
			if (!exec.has_value())
			{
				WriteTraceEntry(trace, i, 'E', 0, false, "malformed", book);
				std::cerr << "Message " << i 
					<< ": malformed 'E' message -- aborting." << std::endl;
				return 1;
			}
			OrderExecute order_exec{ exec->orderId, exec->executedQuantity, exec->matchId };

			bool result = ExecuteOrder(&book, order_exec);
			WriteTraceEntry(trace, i, 'E', exec->orderId, result, result ? nullptr : "not_found", book);
			if (!result)
			{
				std::cerr << "Message " << i << ": ExecuteOrder failed for Order ID = "
					<< (unsigned long long)exec->orderId << " -- aborting." << std::endl;
				return 1;
			}
			break;
		}
		default: {
			std::cerr << "Message " << i 
				<< ": unknown message type'" << msg_type << "' -- aborting." << std::endl;
			return 1;
		}
		}
	}

	std::cout << "Processed " << messages.size() << " messages successfully." << std::endl;
	std::cout << "Final book state: bids=" 
		<< static_cast<int>(book.bid_count) << " asks=" << static_cast<int>(book.ask_count) << std::endl;

	return 0;
}
