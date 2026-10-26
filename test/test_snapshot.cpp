/*
   test_snapshot.cpp
   Byte-level checks of EncodeSnapshotPacket against spec/snapshot.yaml
   (v3), and the trace's write-log line format.
*/

#include <cassert>
#include <cstdio>
#include <sstream>
#include <string>
#include "../include/itchparser.h"
#include "../include/snapshot.h"
#include "../include/trace.h"

static constexpr Price BASE = 1500000;   // $150.00
static Book book;

static void test_packet() {
    static_assert(SNAP_PACKET_LEN == 48);

    ResetBook(&book, BASE);
    AddOrder(&book, Order{1, Side::Buy, BASE - 100, 7});
    AddOrder(&book, Order{2, Side::Buy, BASE - 100, 8});    // same level: 15 shares, 2 orders
    AddOrder(&book, Order{3, Side::Buy, BASE - 300, 50});   // worse bid, not in the BBO
    AddOrder(&book, Order{4, Side::Sell, BASE + 200, 9});

    uint8_t pkt[SNAP_PACKET_LEN];
    EncodeSnapshotPacket(pkt, book, 1u << SNAP_STATUS_UNKNOWN_ORDER_REF,
        0x01020304, 0x1122334455667788ull, 42);

    assert(read_u16_be(pkt, 0) == 0x4F42);
    assert(pkt[2] == 3);
    assert(pkt[3] == 0x08);                        // status passed through
    assert(read_u32_be(pkt, 4) == 0x01020304);
    assert(read_u64_be(pkt, 8) == 0x1122334455667788ull);
    assert(read_u32_be(pkt, 16) == 42);
    assert(read_u64_be(pkt, 20) == 0);             // t_arrive, t_commit
    assert(read_u32_be(pkt, 28) == BASE - 100);    // bid_price
    assert(read_u32_be(pkt, 32) == 15);            // bid_shares
    assert(read_u32_be(pkt, 36) == BASE + 200);    // ask_price
    assert(read_u32_be(pkt, 40) == 9);             // ask_shares
    assert(read_u16_be(pkt, 44) == 2);             // bid_orders
    assert(read_u16_be(pkt, 46) == 1);             // ask_orders

    // An empty side encodes as zeros.
    DeleteOrder(&book, 4);
    EncodeSnapshotPacket(pkt, book, 0, 0, 0, 0);
    assert(read_u32_be(pkt, 36) == 0 && read_u32_be(pkt, 40) == 0 && read_u16_be(pkt, 46) == 0);
    printf("packet: PASS\n");
}

static void test_trace_lines() {
    ResetBook(&book, BASE);
    AddOrder(&book, Order{5, Side::Sell, BASE, 10});

    std::ostringstream out;
    WriteTraceEntry(out, 0, 'A', 5, true, nullptr, book);
    const std::string expected =
        "MSG,0,A,5,OK,\n"
        "W,TBL," + std::to_string(HashOrn(5) * WAYS) + ",1,5,S," + std::to_string(SLOT_CENTER) + ",10\n"
        "W,ASK," + std::to_string(SLOT_CENTER) + ",10,1\n"
        "BBO,0,0,0," + std::to_string(BASE) + ",10,1\n"
        "END\n\n";
    assert(out.str() == expected);

    std::ostringstream header;
    WriteTraceHeader(header, book);
    assert(header.str() == "BOOK," + std::to_string(BASE) + "," + std::to_string(PRICE_TICK) + ","
        + std::to_string(SLOT_CENTER) + "," + std::to_string(SLOTS) + "\n\n");

    std::ostringstream dump;
    WriteStateDump(dump, book);
    assert(dump.str() == "TBL," + std::to_string(HashOrn(5) * WAYS) + ",5,S,"
        + std::to_string(SLOT_CENTER) + ",10\n"
        "ASK," + std::to_string(SLOT_CENTER) + "," + std::to_string(BASE) + ",10,1\n");
    printf("trace_lines: PASS\n");
}

int main() {
    test_packet();
    test_trace_lines();

    printf("\nAll snapshot tests PASSED\n");
    return 0;
}
