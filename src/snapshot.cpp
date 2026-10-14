#include "../include/snapshot.h"
#include "../include/trace.h"

#include <cstring>


static void put_be(uint8_t* p, uint64_t v, const size_t width) {
    for (size_t i = width; i-- > 0; v >>= 8)
        p[i] = static_cast<uint8_t>(v);
}

void EncodeSnapshotPacket(uint8_t* out, const Orderbook& book, const Side side, uint8_t status,
    const uint32_t snapSeq, const uint64_t moldSeq, const uint32_t msgIndex)
{
    std::memset(out, 0, SNAP_PACKET_LEN);

    if (book.bid_count == MAX_ORDERS_PER_SIDE) status |= 1u << SNAP_STATUS_BOOK_FULL_BID;
    if (book.ask_count == MAX_ORDERS_PER_SIDE) status |= 1u << SNAP_STATUS_BOOK_FULL_ASK;

    put_be(out + 0, SNAP_MAGIC, 2);
    out[2] = SNAP_VERSION;
    out[3] = status;
    put_be(out + 4, snapSeq, 4);
    put_be(out + 8, moldSeq, 8);
    put_be(out + 16, msgIndex, 4);
    // [20..27] t_arrive / t_commit: hardware-only, left 0.
    out[28] = book.bid_count;
    out[29] = book.ask_count;
    out[30] = side == Side::Buy ? 0 : 1;

    const SortedOrders sorted = side == Side::Buy
        ? SortSide(book.bids, book.bid_count)
        : SortSide(book.asks, book.ask_count);
    for (uint8_t i = 0; i < sorted.count; i++) {
        uint8_t* lvl = out + SNAP_HDR_LEN + i * SNAP_LEVEL_LEN;
        put_be(lvl + 0, sorted.orders[i].price, 4);
        put_be(lvl + 4, sorted.orders[i].quantity, 4);
        put_be(lvl + 8, sorted.orders[i].orderId, 8);
    }
}
