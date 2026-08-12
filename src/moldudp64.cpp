/*
   moldudp64.cpp
   ----------------
   Implementation of deframe_moldudp64 declared in include/moldudp64.h.
   This file performs a simple, bounds-checked pass over a MoldUDP64
   envelope and returns non-owning views into the inner messages.
*/

#include "../include/moldudp64.h"

std::vector<MoldUDPMessage> deframe_moldudp64(const uint8_t* buf, const size_t buf_len) {
	std::vector<MoldUDPMessage> messages;

	if (buf_len < MOLDUDP64_HEADER_LEN)
	{
		return messages;
	}

	const uint16_t message_count = read_u16_be(buf, MOLDUDP64_MSGCOUNT_OFFSET);

	if (message_count == MOLDUDP64_HEARTBEAT || message_count == MOLDUDP64_END_OF_SESSION)
	{
		return messages;
	}

	size_t cursor = MOLDUDP64_HEADER_LEN;

	for (uint16_t i = 0; i < message_count; i++)
	{
		if (cursor + 2 > buf_len) 
		{
			return {};
		}

		const uint16_t msg_len = read_u16_be(buf, cursor);
		cursor += 2;

		if (cursor + msg_len > buf_len)
		{
			return {};
		}

		messages.push_back(MoldUDPMessage{ buf + cursor, msg_len });
		cursor += msg_len;
	}

	return messages;

}
