/*
   test_snapshot.cpp
   Byte-level checks of EncodeSnapshotPacket against spec/snapshot.yaml.
*/

#include <cassert>
#include <cstdio>
#include "../include/itchparser.h"
#include "../include/snapshot.h"

int main() {
    static_assert(SNAP_PACKET_LEN == 1056 || MAX_ORDERS_PER_SIDE != 64);

    Orderbook ob{};
    AddOrder(&ob, Order{30, Side::Buy, 100, 7});
    AddOrder(&ob, Order{10, Side::Buy, 101, 8});
    AddOrder(&ob, Order{20, Side::Sell, 200, 9});

    uint8_t pkt[SNAP_PACKET_LEN];
    EncodeSnapshotPacket(pkt, ob, Side::Buy, 1u << SNAP_STATUS_UNKNOWN_ORDER_REF,
        0x01020304, 0x1122334455667788ull, 42);

    // Header
    assert(read_u16_be(pkt, 0) == 0x4F42);
    assert(pkt[2] == 2);
    assert(pkt[3] == 0x08);                       // unknown_order_ref only
    assert(read_u32_be(pkt, 4) == 0x01020304);
    assert(read_u64_be(pkt, 8) == 0x1122334455667788ull);
    assert(read_u32_be(pkt, 16) == 42);
    assert(read_u64_be(pkt, 20) == 0);            // t_arrive, t_commit
    assert(pkt[28] == 2 && pkt[29] == 1);         // both counts in every packet
    assert(pkt[30] == 0 && pkt[31] == 0);         // side_id bids, reserved

    // Levels sorted ascending by orderId: id10 first despite later insert.
    assert(read_u32_be(pkt, 32) == 101 && read_u32_be(pkt, 36) == 8 && read_u64_be(pkt, 40) == 10);
    assert(read_u32_be(pkt, 48) == 100 && read_u32_be(pkt, 52) == 7 && read_u64_be(pkt, 56) == 30);
    for (size_t i = 64; i < SNAP_PACKET_LEN; i++) assert(pkt[i] == 0);   // zero-filled tail

    // Ask packet + book_full flag derived from the book.
    for (OrderId id = 100; ob.ask_count < MAX_ORDERS_PER_SIDE; id++)
        AddOrder(&ob, Order{id, Side::Sell, 300, 1});
    EncodeSnapshotPacket(pkt, ob, Side::Sell, 0, 1, 0, 0);
    assert(pkt[3] == 1u << SNAP_STATUS_BOOK_FULL_ASK);
    assert(pkt[30] == 1);
    assert(read_u64_be(pkt, 40) == 20);           // lowest orderId first

    printf("All snapshot tests PASSED\n");
    return 0;
}
