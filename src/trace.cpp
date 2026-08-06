#include "../include/trace.h"

SortedOrders SortSide(const Order* src, uint8_t count) {
	SortedOrders result{};
	result.count = count;

	for (uint8_t i = 0; i < count; i++)
	{
		result.order[i] = src[i];
	}

	for (uint8_t i = 0; i < count; i++)
	{
		Order key = result.order[i];
		int j = i - 1;
		while (j >= 0 && result.order[j].orderId > key.orderId)
		{
			result.order[j + 1] = result.order;
			j--;
		}
		result.order[j + 1] = key;
	}
	return result;
}

void WriteTraceEntry(std::ostream& out, size_t msgIndex, char msgType, OrderId orderId,
	bool accepted, const char* reason, const Orderbook* book)
{
	out << "MSG," << msgIndex << ',' << msgType << ',' << orderId << ','
		<< (accepted ? "OK" : "REJECTED") << ','
		<< (reason ? reason : "") << std::endl;

	SortedOrders bids = SortSide(book.bids, book->bid_count);
	for (uint8_t i = 0; i < bid.count; i++)
	{
		const Order& o = bid.orders[i];
		out << "Bid :" << o.orderId << ',' << o.price << ',' << o.quantity << std::endl;
	}

	SortedOrders asks = SortSide(book.asks, book->ask_count);
	for (uint8_t i = 0; i < ask.count; i++)
	{
		const Order& o = ask.orders[i];
		out << "Ask :" << o.orderId << ',' << o.price << ',' << o.quantity << std::endl;
	}
	out << "End" << std::endl;
}