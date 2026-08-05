#include <cassert>
#include <cstdio>
#include <cstring>

#include "../include/moldudp64.h"

static void test_deframe_two_messages() {
    // Header (20 bytes) + two message blocks:
    //   Block 1: length=3, data={0xAA, 0xBB, 0xCC}
    //   Block 2: length=2, data={0x11, 0x22}
    // Total buffer size = 20 + (2+3) + (2+2) = 29 bytes

    uint8_t buf[29] = { 0 };

    // Session (offset 0-9) — left as 0, deframe_moldudp64 doesn't need it
    // Sequence Number (offset 10-17) — left as 0, not used

    // Message Count = 2 at offset 18 (2 bytes, big-endian)
    buf[18] = 0x00; buf[19] = 0x02;

    // Message Block 1, starts at offset 20
    buf[20] = 0x00; buf[21] = 0x03;               // length = 3
    buf[22] = 0xAA; buf[23] = 0xBB; buf[24] = 0xCC; // data

    // Message Block 2, starts at offset 25
    buf[25] = 0x00; buf[26] = 0x02;               // length = 2
    buf[27] = 0x11; buf[28] = 0x22;                // data

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.size() == 2);

    assert(messages[0].length == 3);
    assert(messages[0].data[0] == 0xAA);
    assert(messages[0].data[1] == 0xBB);
    assert(messages[0].data[2] == 0xCC);

    assert(messages[1].length == 2);
    assert(messages[1].data[0] == 0x11);
    assert(messages[1].data[1] == 0x22);

    printf("deframe_moldudp64 (two messages): PASS\n");
    printf("  messages.size()=%zu\n", messages.size());
}

static void test_deframe_zero_extra_messages() {
    // Header only, Message Count = 0 messages worth of payload but
    // Message Count field itself set to a normal small value: 1 message, empty data.
    // (Message Data "can be zero length" per spec — worth covering.)

    uint8_t buf[22] = { 0 };

    // Message Count = 1 at offset 18
    buf[18] = 0x00; buf[19] = 0x01;

    // Message Block 1, starts at offset 20 — length = 0, no data bytes follow
    buf[20] = 0x00; buf[21] = 0x00;

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.size() == 1);
    assert(messages[0].length == 0);

    printf("deframe_moldudp64 (zero-length message): PASS\n");
}

static void test_deframe_heartbeat_rejected() {
    // Message Count = 0x0000 (Heartbeat) — out of scope, must return empty.
    uint8_t buf[MOLDUDP64_HEADER_LEN] = { 0 };
    buf[18] = 0x00; buf[19] = 0x00;

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.empty());
    printf("deframe_moldudp64 (heartbeat rejected): PASS\n");
}

static void test_deframe_end_of_session_rejected() {
    // Message Count = 0xFFFF (End of Session) — out of scope, must return empty.
    uint8_t buf[MOLDUDP64_HEADER_LEN] = { 0 };
    buf[18] = 0xFF; buf[19] = 0xFF;

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.empty());
    printf("deframe_moldudp64 (end-of-session rejected): PASS\n");
}

static void test_deframe_truncated_header() {
    // Buffer shorter than the 20-byte header itself.
    uint8_t buf[10] = { 0 };

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.empty());
    printf("deframe_moldudp64 (truncated header): PASS\n");
}

static void test_deframe_truncated_message_block() {
    // Header claims Message Count = 1, but buffer is cut off
    // partway through that message's declared length.
    uint8_t buf[23] = { 0 };

    buf[18] = 0x00; buf[19] = 0x01;   // Message Count = 1

    // Message Block 1, starts at offset 20 — claims length = 10,
    // but buffer ends at offset 22 (only 2 bytes of data actually present).
    buf[20] = 0x00; buf[21] = 0x0A;   // length = 10 (declared, but not enough room)

    auto messages = deframe_moldudp64(buf, sizeof(buf));

    assert(messages.empty());
    printf("deframe_moldudp64 (truncated message block): PASS\n");
}

int main() {
    test_deframe_two_messages();
    test_deframe_zero_extra_messages();
    test_deframe_heartbeat_rejected();
    test_deframe_end_of_session_rejected();
    test_deframe_truncated_header();
    test_deframe_truncated_message_block();

    printf("\nAll moldudp64 tests PASSED\n");
    return 0;
}