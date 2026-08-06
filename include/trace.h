#pragma once 

#include <cstdint>
#include <ostream>

#include "orderbook.h"

/* SortedOrders
   A fixed-capacity, orderId-sorted copy of one side of the book.
   Sorting by orderId canonicalizes the dump so it's independent of
   whatever internal slot/array layout the book (or, later, the RTL's
   storage scheme) happens to use -- two implementations with different
   internal layouts but the same logical resting orders will produce
   an identical dump.
*/

struct SortedOrders{
	
	Order orders[32];
	uint8_t count;

};

/* SortSide
   Copies 'src' (count entries) into 'out', sorted ascending by orderId.
   Simple insertion sort -- fine at this size (max 32), this runs on a
   debug/verification path, not the hot path, so efficiency is not a
   concern here.
*/

SortedOrders SortSide(const Order* src, uint8_t count);

/* WriteTraceEntry
   Emits one message's full book-state dump:
     MSG,<index>,<type>,<orderId>,<result>,<reason>
     BID,<orderId>,<price>,<qty>      (one line per resting bid, sorted)
     ...
     ASK,<orderId>,<price>,<qty>      (one line per resting ask, sorted)
     ...
     END

   Called once per message, after the message's outcome (accept/reject)
   is known, so the trace file is a complete message-by-message oracle
   for comparison against RTL testbench readback.
*/

void WriteTraceEntry(std::ostream& out, size_t msgIndex, char msgType, OrderId orderId, 
    bool accepted, const char* reason, const Orderbook& book);