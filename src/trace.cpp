#include "../include/trace.h"

SortedOrders SortSide(const Order* src, uint8_t count) {
    SortedOrders result{};
    result.count = count;

    for (uint8_t i = 0; i < count; i++)
    {
        result.orders[i] = src[i];
    }

    // Insertion sort ascending by orderId.
    for (uint8_t i = 1; i < count; i++)
    {
        Order key = result.orders[i];
        int j = i - 1;
        while (j >= 0 && result.orders[j].orderId > key.orderId)
        {
            result.orders[j + 1] = result.orders[j];
            j--;
        }
        result.orders[j + 1] = key;
    }
    return result;
}

void WriteTraceEntry(std::ostream& out, size_t msgIndex, char msgType, OrderId orderId,
    bool accepted, const char* reason, const Orderbook& book)
{
    out << "MSG," << msgIndex << ',' << msgType << ',' << orderId << ','
        << (accepted ? "OK" : "REJECTED") << ','
        << (reason ? reason : "") << '\n';

    SortedOrders bids = SortSide(book.bids, book.bid_count);
    for (uint8_t i = 0; i < bids.count; i++)
    {
        const Order& o = bids.orders[i];
        out << "BID," << o.orderId << ',' << o.price << ',' << o.quantity << '\n';
    }

    SortedOrders asks = SortSide(book.asks, book.ask_count);
    for (uint8_t i = 0; i < asks.count; i++)
    {
        const Order& o = asks.orders[i];
        out << "ASK," << o.orderId << ',' << o.price << ',' << o.quantity << '\n';
    }
    out << "END\n\n" ;
}