#include <cassert>
#include <cstdio>

#include "../include/itchparser.h"

static void test_parse_add() {
    // Hand-built 'A' message, 36 bytes total.
    uint8_t buf[ADD_ORDER_LEN] = { 0 };

    buf[0] = 'A';   // Message Type

    // Order Reference Number = 12345 at offset 11 (8 bytes, big-endian)
    buf[11] = 0; buf[12] = 0; buf[13] = 0; buf[14] = 0;
    buf[15] = 0; buf[16] = 0; buf[17] = 0x30; buf[18] = 0x39;

    buf[19] = 'B';   // Buy/Sell Indicator

    // Shares = 100 at offset 20 (4 bytes)
    buf[20] = 0; buf[21] = 0; buf[22] = 0; buf[23] = 100;

    // Stock field (offset 24-31) left as 0 — not read by parse_add

    // Price = 250000 at offset 32 (4 bytes) -> 0x0003D090
    buf[32] = 0x00; buf[33] = 0x03; buf[34] = 0xD0; buf[35] = 0x90;

    auto result = parse_add(buf, ADD_ORDER_LEN, 0);

    assert(result.has_value());
    assert(result->orderId == 12345);
    assert(result->side == Side::Buy);
    assert(result->quantity == 100);
    assert(result->price == 250000);

    printf("parse_add: PASS\n");
    printf("  orderId=%llu side=%s quantity=%u price=%u\n",
        (unsigned long long)result->orderId,
        result->side == Side::Buy ? "Buy" : "Sell",
        result->quantity,
        result->price);

    auto truncated = parse_add(buf, 20, 0);  // only 20 bytes, need 36
    assert(!truncated.has_value());
    printf("parse_add (truncated): PASS -- correctly returned nullopt\n");
}

static void test_parse_delete() {
    // Hand-built 'D' message, 19 bytes total.
    uint8_t buf[ORDER_DELETE_LEN] = { 0 };

    buf[0] = 'D';   // Message Type

    // Order Reference Number = 54321 at offset 11 (8 bytes) -> 0xD431
    buf[11] = 0; buf[12] = 0; buf[13] = 0; buf[14] = 0;
    buf[15] = 0; buf[16] = 0; buf[17] = 0xD4; buf[18] = 0x31;

    auto result = parse_delete(buf, ORDER_DELETE_LEN, 0);

    assert(result.has_value());
    assert(*result == 54321);

    printf("parse_delete: PASS\n");
    printf("  orderId=%llu\n", (unsigned long long) * result);

    auto truncated = parse_delete(buf, 15, 0);  // only 15 bytes, need 19
    assert(!truncated.has_value());
    printf("parse_delete (truncated): PASS -- correctly returned nullopt\n");
}

static void test_parse_replace() {
    // Hand-built 'U' message, 35 bytes total.
    uint8_t buf[ORDER_REPLACE_LEN] = { 0 };

    buf[0] = 'U';   // Message Type

    // Original Order Reference Number = 12345 at offset 11 -> 0x3039
    buf[11] = 0; buf[12] = 0; buf[13] = 0; buf[14] = 0;
    buf[15] = 0; buf[16] = 0; buf[17] = 0x30; buf[18] = 0x39;

    // New Order Reference Number = 67890 at offset 19 -> 0x00010932
    buf[19] = 0; buf[20] = 0; buf[21] = 0; buf[22] = 0;
    buf[23] = 0; buf[24] = 0x01; buf[25] = 0x09; buf[26] = 0x32;

    // Shares = 200 at offset 27 (4 bytes)
    buf[27] = 0; buf[28] = 0; buf[29] = 0; buf[30] = 200;

    // Price = 300000 at offset 31 (4 bytes) -> 0x000493E0
    buf[31] = 0x00; buf[32] = 0x04; buf[33] = 0x93; buf[34] = 0xE0;

    auto result = parse_replace(buf, ORDER_REPLACE_LEN, 0);

    assert(result.has_value());
    assert(result->OldOrderId == 12345);
    assert(result->NewOrderId == 67890);
    assert(result->quantity == 200);
    assert(result->price == 300000);

    printf("parse_replace: PASS\n");
    printf("  oldOrderId=%llu newOrderId=%llu quantity=%u price=%u\n",
        (unsigned long long)result->OldOrderId,
        (unsigned long long)result->NewOrderId,
        result->quantity,
        result->price);

    auto truncated = parse_replace(buf, 30, 0);  // only 30 bytes, need 35
    assert(!truncated.has_value());
    printf("parse_replace (truncated): PASS -- correctly returned nullopt\n");
}

int main() {
    test_parse_add();
    test_parse_delete();
    test_parse_replace();

    printf("\nAll itchparser tests PASSED\n");
    return 0;
}