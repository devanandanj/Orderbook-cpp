#pragma once

#include <cstdint>
#include <vector>

#include "../include/itchparser.h"

static constexpr size_t MOLDUDP64_HEADER_LEN = 20;
static constexpr size_t MOLDUDP64_SESSION_OFFSET = 0;
static constexpr size_t MOLDUDP64_SESSION_LEN = 10;
static constexpr size_t MOLDUDP64_SEQNUM_OFFSET = 10;
static constexpr size_t MOLDUDP64_MSGCOUNT_OFFSET = 18;

static constexpr uint16_t MOLDUDP64_HEARTBEAT = 0x0000;
static constexpr uint16_t MOLDUDP64_END_OF_SESSION = 0xFFFF;

// One de-framed msg : view into original buffer

struct MoldUDPMessage
{
	const uint8_t* data;
	uint16_t length;
};

// Strips the 20-byte MoldUDP64 header and yields each message block's
// (pointer, length) in order. Assumes a well-formed, complete buffer —
// no heartbeat/end-of-session/gap-recovery handling (file-based synthetic
// input only; generator must never emit Message Count 0x0000 or 0xFFFF).
// Returns empty vector if the buffer is too short to even hold the header,
// or if a message block's declared length would run past the buffer end.
std::vector<MoldUDPMessage> deframe_moldudp64(const uint8_t* buf, size_t buf_len);