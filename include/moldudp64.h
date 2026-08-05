/*
   moldudp64.h
   ----------------
   Lightweight helpers for working with MoldUDP64-framed input used in
   the test data/generator for this project. The real MoldUDP64 protocol
   is a simple framing envelope that prefixes a block of messages with a
   20-byte header. This header contains a session identifier, a sequence
   number and a message count. Each message inside the envelope is then
   stored sequentially.

   This header-only API focuses on deframing that envelope into a list
   of (pointer, length) views into the original buffer so higher-level
   parsers can decode the inner messages without copying memory.
*/

#pragma once

#include <cstdint>
#include <vector>

#include "../include/itchparser.h"

/* Header layout constants (all sizes in bytes):
   - MOLDUDP64_HEADER_LEN: total header length (20 bytes)
   - MOLDUDP64_SESSION_OFFSET: offset where the session id begins (0)
   - MOLDUDP64_SESSION_LEN: length of the session id field (10 bytes)
   - MOLDUDP64_SEQNUM_OFFSET: offset where the 8-byte sequence number begins (10)
   - MOLDUDP64_MSGCOUNT_OFFSET: offset where the 2-byte message count begins (18)
*/
static constexpr size_t MOLDUDP64_HEADER_LEN = 20;
static constexpr size_t MOLDUDP64_SESSION_OFFSET = 0;
static constexpr size_t MOLDUDP64_SESSION_LEN = 10;
static constexpr size_t MOLDUDP64_SEQNUM_OFFSET = 10;
static constexpr size_t MOLDUDP64_MSGCOUNT_OFFSET = 18;

/* Special message-count values used by the generator used for tests:
   - 0x0000 indicates heartbeat (no messages)
   - 0xFFFF indicates end-of-session
   The deframer in this codebase intentionally ignores these control
   values because the test data generator never emits them for regular
   message files.
*/
static constexpr uint16_t MOLDUDP64_HEARTBEAT = 0x0000;
static constexpr uint16_t MOLDUDP64_END_OF_SESSION = 0xFFFF;

/* MoldUDPMessage
   A non-owning view into one inner message inside a MoldUDP64 envelope.
   Fields:
   - data: pointer into the original buffer where this message's payload begins
   - length: number of bytes in the message payload

   Note: this struct does NOT own the memory pointed to by data. The caller
   must ensure the original buffer remains valid while any MoldUDPMessage
   views are in use.
*/
struct MoldUDPMessage
{
	const uint8_t* data;
	const uint16_t length;
};

/* deframe_moldudp64
   ------------------
   Extracts the sequence of inner messages from a MoldUDP64-framed buffer.
   Parameters:
   - buf: pointer to the start of the incoming byte buffer (raw bytes)
   - buf_len: total number of valid bytes at buf

   Behaviour and guarantees:
   - If buf_len is smaller than MOLDUDP64_HEADER_LEN, the function returns
	 an empty vector because there is not even a complete envelope header.
   - Otherwise the function reads the message count from the header and
	 iterates over the declared message blocks, producing a MoldUDPMessage
	 view for each. It validates that each declared block fits inside
	 buf_len; if any block claims a length that would run past the end of
	 the buffer the function returns an empty vector to signal malformed data.
   - This deframer is intentionally simple: it does not attempt to handle
	 heartbeats (message count 0x0000), end-of-session, or gap recovery.
	 It is designed for file-based synthetic input produced by the project's
	 generator where those cases do not occur.

   Return value:
   - A vector of MoldUDPMessage entries, one per inner message. Each entry
	 points into the original buffer and therefore does not allocate or copy
	 the message payload.
*/
std::vector<MoldUDPMessage> deframe_moldudp64(const uint8_t* buf, size_t buf_len);
