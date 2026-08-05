/*
   gen_test.cpp
   ----------------
   Small helper program that generates a MoldUDP64 test file containing
   several hand-crafted ITCH messages. The resulting binary can be fed
   into the main program to exercise parsing and orderbook behavior.

   The helper functions below append unsigned integers in big-endian
   byte order to a std::vector<uint8_t> which is used as the packet
   payload builder.
*/

#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>

/* Append a 16-bit unsigned integer in big-endian order to buf. */
static void push_u16_be(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back(val & 0xFF);
}

/* Append a 32-bit unsigned integer in big-endian order to buf. */
static void push_u32_be(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back((val >> 24) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back(val & 0xFF);
}

/* Append a 64-bit unsigned integer in big-endian order to buf. */
static void push_u64_be(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 56; i >= 0; i -= 8) {
        buf.push_back((val >> i) & 0xFF);
    }
}

/* Build an ITCH 'A' (Add Order) message payload. The returned vector
   contains the full message bytes (36 bytes in the test format). */
static std::vector<uint8_t> make_itch_add(uint64_t orderId, char side, uint32_t qty, uint32_t price) {
    std::vector<uint8_t> msg;
    msg.push_back('A');
    push_u16_be(msg, 1);                  /* Stock Locate */
    push_u16_be(msg, 0);                  /* Tracking Number */
    push_u64_be(msg, 1000000);            /* Timestamp (dummy) -> 6 bytes */
    msg.erase(msg.end() - 2, msg.end());
    push_u64_be(msg, orderId);
    msg.push_back(static_cast<uint8_t>(side));
    push_u32_be(msg, qty);

    const char* stock = "AAPL    ";
    msg.insert(msg.end(), stock, stock + 8);

    push_u32_be(msg, price);
    return msg;
}

/* Build an ITCH 'U' (Replace Order) message payload. */
static std::vector<uint8_t> make_itch_replace(uint64_t oldOrderId, uint64_t newOrderId, uint32_t qty, uint32_t price) {
    std::vector<uint8_t> msg;
    msg.push_back('U');
    push_u16_be(msg, 1);                  /* Stock Locate */
    push_u16_be(msg, 0);                  /* Tracking Number */
    push_u64_be(msg, 1000000);            /* Timestamp -> 6 bytes */
    msg.erase(msg.end() - 2, msg.end());
    push_u64_be(msg, oldOrderId);
    push_u64_be(msg, newOrderId);
    push_u32_be(msg, qty);
    push_u32_be(msg, price);
    return msg;
}

/* Build an ITCH 'D' (Delete Order) message payload. */
static std::vector<uint8_t> make_itch_delete(uint64_t orderId) {
    std::vector<uint8_t> msg;
    msg.push_back('D');
    push_u16_be(msg, 1);                  /* Stock Locate */
    push_u16_be(msg, 0);                  /* Tracking Number */
    push_u64_be(msg, 1000000);            /* Timestamp -> 6 bytes */
    msg.erase(msg.end() - 2, msg.end());
    push_u64_be(msg, orderId);
    return msg;
}

/* Build an ITCH 'E' (Order Executed) message payload. */
static std::vector<uint8_t> make_itch_execute(uint64_t orderId, uint32_t execQty, uint64_t matchId) {
    std::vector<uint8_t> msg;
    msg.push_back('E');
    push_u16_be(msg, 1);                  /* Stock Locate */
    push_u16_be(msg, 0);                  /* Tracking Number */
    push_u64_be(msg, 1000000);            /* Timestamp -> 6 bytes */
    msg.erase(msg.end() - 2, msg.end());
    push_u64_be(msg, orderId);
    push_u32_be(msg, execQty);
    push_u64_be(msg, matchId);
    return msg;
}

int main(int argc, char** argv) {
    const char* filename = (argc > 1) ? argv[1] : "test_feed.mold";

    std::vector<std::vector<uint8_t>> itch_msgs;

    /* Test case sequence: a small set of messages to exercise the
       parser and orderbook logic. Each vector is a single ITCH
       message payload constructed by the helper functions above. */
    /* 1. Add Bid: ID 101, Buy, 100 shares @ $150.00 */
    itch_msgs.push_back(make_itch_add(101, 'B', 100, 1500000));
    /* 2. Add Ask: ID 102, Sell, 50 shares @ $151.00 */
    itch_msgs.push_back(make_itch_add(102, 'S', 50, 1510000));
    /* 3. Add Bid: ID 104, Buy, 200 shares @ $149.50 */
    itch_msgs.push_back(make_itch_add(104, 'B', 200, 1495000));

    /* 4. Partial Execution ('E'): Execute 40 shares of Order 101 (60 shares remain) */
    itch_msgs.push_back(make_itch_execute(101, 40, 900001));

    /* 5. Full Execution ('E'): Execute remaining 60 shares of Order 101 (Order 101 removed) */
    itch_msgs.push_back(make_itch_execute(101, 60, 900002));

    /* 6. Replace Order ('U'): ID 104 -> ID 105, 250 shares @ $150.00 */
    itch_msgs.push_back(make_itch_replace(104, 105, 250, 1500000));

    /* 7. Delete Order ('D'): Delete Ask Order 102 */
    itch_msgs.push_back(make_itch_delete(102));

    /* Build MoldUDP64 Buffer: session id (10 bytes), sequence number
       (8 bytes), message count (2 bytes) followed by message blocks
       (each with a 2-byte length prefix). */
    std::vector<uint8_t> mold_packet;

    char session[10] = "SESSION01";
    mold_packet.insert(mold_packet.end(), session, session + 10);
    push_u64_be(mold_packet, 1);                    /* Sequence Number = 1 */
    push_u16_be(mold_packet, static_cast<uint16_t>(itch_msgs.size()));     /* Message Count = 7 */

    for (const auto& msg : itch_msgs) {
        push_u16_be(mold_packet, static_cast<uint16_t>(msg.size()));
        mold_packet.insert(mold_packet.end(), msg.begin(), msg.end());
    }

    FILE* f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Failed to open file %s for writing\n", filename);
        return 1;
    }

    fwrite(mold_packet.data(), 1, mold_packet.size(), f);
    fclose(f);

    printf("Successfully generated MoldUDP64 test file '%s' (%zu bytes, %zu ITCH messages).\n",
        filename, mold_packet.size(), itch_msgs.size());

    return 0;
}