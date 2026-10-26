/*
   snapshot.h
   ----------------
   Binary book snapshot -- the byte-exact verification trace shared with
   the RTL. Layout is owned by hardware-orderbook/spec/snapshot.yaml;
   the constants below must match spec/generated/itch_defs.h.

   Version 3: one fixed 48-byte big-endian packet per BOOK message (A F E
   C X D U for the selected symbol) carrying the best bid and offer; other
   messages produce none, and msg_index counts every message in the
   stream. Deep-book state is not on the wire; it is
   checked in simulation from the book's WriteLog (see book.h).
   t_arrive / t_commit are hardware cycle counters; the model writes 0 and
   the scoreboard masks them.
*/

#pragma once

#include <cstddef>
#include <cstdint>

#include "book.h"

constexpr uint16_t SNAP_MAGIC = 0x4F42;   // "OB"
constexpr uint8_t SNAP_VERSION = 3;
constexpr size_t SNAP_PACKET_LEN = 48;

// Status word bit positions.
constexpr uint8_t SNAP_STATUS_ORDER_DROPPED = 0;
constexpr uint8_t SNAP_STATUS_PRICE_REJECTED = 1;
constexpr uint8_t SNAP_STATUS_MOLD_GAP_SEEN = 2;
constexpr uint8_t SNAP_STATUS_UNKNOWN_ORDER_REF = 3;

/* EncodeSnapshotPacket
   Fills `out` (SNAP_PACKET_LEN bytes) with the BBO packet for the message
   just applied. `status` carries that message's outcome bits.
*/
void EncodeSnapshotPacket(uint8_t* out, const Book& book, uint8_t status,
    uint32_t snapSeq, uint64_t moldSeq, uint32_t msgIndex);
