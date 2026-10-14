/*
   snapshot.h
   ----------------
   Binary book snapshot -- the byte-exact verification trace shared with
   the RTL. Layout is owned by hardware-orderbook/spec/snapshot.yaml;
   the constants below must match spec/generated/itch_defs.h.

   One ITCH message produces TWO fixed-size packets: bids (side_id 0)
   then asks (side_id 1). Each is a 32-byte big-endian header followed by
   MAX_ORDERS_PER_SIDE 16-byte level slots, sorted ascending by orderId,
   zero-filled past the side's count. t_arrive / t_commit are hardware
   cycle counters; the model writes 0 and the scoreboard masks them.
*/

#pragma once

#include <cstddef>
#include <cstdint>

#include "orderbook.h"

constexpr uint16_t SNAP_MAGIC = 0x4F42;   // "OB"
constexpr uint8_t SNAP_VERSION = 2;
constexpr size_t SNAP_HDR_LEN = 32;
constexpr size_t SNAP_LEVEL_LEN = 16;
constexpr size_t SNAP_PACKET_LEN = SNAP_HDR_LEN + MAX_ORDERS_PER_SIDE * SNAP_LEVEL_LEN;

// Status word bit positions.
constexpr uint8_t SNAP_STATUS_BOOK_FULL_BID = 0;
constexpr uint8_t SNAP_STATUS_BOOK_FULL_ASK = 1;
constexpr uint8_t SNAP_STATUS_MOLD_GAP_SEEN = 2;
constexpr uint8_t SNAP_STATUS_UNKNOWN_ORDER_REF = 3;

/* EncodeSnapshotPacket
   Fills `out` (SNAP_PACKET_LEN bytes) with one side's packet.
   `status` should carry only the message-level bits (mold_gap_seen,
   unknown_order_ref); book_full_* are derived from the book here so
   they can never disagree with the counts.
*/
void EncodeSnapshotPacket(uint8_t* out, const Orderbook& book, Side side, uint8_t status,
    uint32_t snapSeq, uint64_t moldSeq, uint32_t msgIndex);
