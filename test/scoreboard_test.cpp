///*
//   scoreboard_test.cpp
//   --------------------
//   Lightweight verification harness that walks a precomputed sequence of
//   expected outcomes and checks the Orderbook counts after each message.
//   This file is intended as a human-readable checklist rather than a
//   full automated unit test; replace the "ProcessMessage" placeholder
//   with your actual dispatch logic for automated verification.
//*/
//
//#include <iostream>
//#include <vector>
//#include <cstdint>
//#include "../include/orderbook.h"
//#include "../include/itchparser.h"
//#include "../include/moldudp64.h"
//
///* ExpectedOutcome
//   Describes the expected book state after processing a particular
//   message from the validated trace. Fields:
//   - msgIndex: index of the message in the trace
//   - msgType: single-character ITCH message type (A, D, U, E, ...)
//   - orderId: primary order reference the expectation relates to
//   - accepted: whether the message was accepted (true) or discarded (false)
//   - expectedBidCount / expectedAskCount: counts expected after processing
//*/
//struct ExpectedOutcome {
//    int msgIndex;
//    char msgType;
//    uint64_t orderId;
//    bool accepted;          /* false for Discarded */
//    uint8_t expectedBidCount;
//    uint8_t expectedAskCount;
//};
//
///* Ground truth extracted from the validated trace. Populate this
//   vector with the full expected sequence from your trace. The small
//   example below demonstrates a few representative cases. */
//const std::vector<ExpectedOutcome> expected = {
//    {0,  'A', 1,   true,  1,  0},
//    {1,  'A', 2,   true,  2,  0},
//    /* ... populate indices 2 through 30 as needed ... */
//    {31, 'A', 32,  true,  32, 0},   /* Array full (32 items) */
//    {32, 'A', 33,  true,  32, 0},   /* Evicted worst bid, count stays 32 */
//    {33, 'A', 34,  false, 32, 0},   /* Discarded (lower than worst bid) */
//    {34, 'A', 35,  false, 32, 0},   /* Discarded */
//    {35, 'D', 2,   true,  31, 0},   /* Cancel frees a slot (count drops to 31) */
//    {36, 'A', 36,  true,  32, 0},   /* Reinsert fills slot back to 32 */
//    {37, 'E', 36,  true,  32, 0},   /* Execute order, count unchanged */
//    {38, 'U', 200, true,  32, 0},   /* Replace order, count unchanged */
//    {39, 'A', 101, true,  32, 1},   /* First ask inserted */
//    {40, 'A', 102, true,  32, 2},   /* Second ask inserted */
//    {41, 'A', 103, true,  32, 3}    /* Third ask inserted */
//};
//
//int main() {
//    auto messages = deframe_moldudp64("../stress_test.mold");
//    if (messages.empty()) {
//        std::cerr << "[ERROR] Failed to load stress_test.mold or file is empty!\n";
//        return 1;
//    }
//    Orderbook book{};
//    // Initialize book state...
//    int failures = 0;
//
//    for (const auto& exp : expected) {
//        // Call your dispatch logic here on the corresponding test message
//        // bool accepted = ProcessMessage(book, msg);
//        bool ok = true;
//
//        // Verify state against expectations
//        if (book.bid_count != exp.expectedBidCount) { ok = false; }
//        if (book.ask_count != exp.expectedAskCount) { ok = false; }
//
//        if (!ok) {
//
//            std::cerr << "[FAIL] Msg " << exp.msgIndex<< " ('" << exp.msgType << "'): "
//                << " Expected (Bids:" << (int)exp.expectedBidCount << ", Asks:" << (int)exp.expectedAskCount << ")" 
//                << " Got (Bids:" << (int)book.bid_count << ", Asks:" << (int)book.ask_count << ")\n";
//            failures++;
//        }
//    }
//
//    if (failures == 0) {
//        std::cout << "[PASS] All " << expected.size() << " checks passed successfully.\n";
//        return 0;
//    }
//    else {
//        std::cerr << "[FAIL] " << failures << " check(s) failed.\n";
//        return 1;
//    }
//}
//*/