#include "../include/moldudp64.h"

std::vector<MoldUDPMessage> deframe_moldudp64(const uint8_t* buf, size_t buf_len) {
	std::vector<MoldUDPMessage> messages;
	
	if (buf_len < MOLDUDP64_HEADER_LEN)
	{
		return messages; //empty - too short for even header
	}

	uint16_t message_count = read_u16_be(buf, MOLDUDP64_MSGCOUNT_OFFSET);
	
	// Out-of-scope sentinel values for file-based synthetic input.
	// Generator is responsible for never producing these.

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
		
		uint16_t msg_len = read_u16_be(buf, cursor);
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