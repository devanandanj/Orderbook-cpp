#pragma once

#include <cstdint>
#include <ostream>

#include "book.h"

/* Debug and verification text dumps. Not the model's product output (that
   is snapshot.bin); these are for eyeballing and for the in-simulation
   memory compare.
*/

/* WriteTraceHeader
   First line of trace.txt: the geometry needed to turn slots back into
   prices (price = base + (slot - center) * tick), so a reader such as
   tools/show_book.py needs nothing but the trace.
     BOOK,<basePrice>,<tick>,<center>,<slots>
*/
void WriteTraceHeader(std::ostream& out, const Book& book);

/* WriteTraceEntry
   Emits one message's outcome, the memory writes it caused and the BBO:
     MSG,<index>,<type>,<orderId>,<result>,<reason>
     W,...                        (one line per WriteLog record, in order)
     BBO,<bidPx>,<bidQty>,<bidN>,<askPx>,<askQty>,<askN>
     END

   The W lines are exactly what the testbench checks against the RTL's
   memory write ports:
     W,TBL,<set*WAYS+way>,<valid>,<orderId>,<side>,<slot>,<shares>
     W,CAM,<entry>,<valid>,<orderId>,<side>,<slot>,<shares>
     W,BID,<slot>,<shares>,<orders>
     W,ASK,<slot>,<shares>,<orders>
   <side> is B or S.
*/
void WriteTraceEntry(std::ostream& out, size_t msgIndex, char msgType, OrderId orderId,
    bool accepted, const char* reason, const Book& book);

/* WriteStateDump
   Every valid table/CAM entry and every occupied level, in address order.
   Taken at end of run and compared against a readback of the RTL's
   memories, it catches anything the per-message write compare missed.
     TBL,<set*WAYS+way>,<orderId>,<side>,<slot>,<shares>
     CAM,<entry>,<orderId>,<side>,<slot>,<shares>
     BID,<slot>,<price>,<shares>,<orders>
     ASK,<slot>,<price>,<shares>,<orders>
*/
void WriteStateDump(std::ostream& out, const Book& book);
