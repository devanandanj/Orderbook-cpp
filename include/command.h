/*
   command.h
   ----------------
   The normalised book command: what the parser hands the book, in the
   model exactly as in the RTL (where it is the interface between the
   cut-through parser and the book core). One shape for all seven message
   types, so the main loop, the C API and the testbench all drive the book
   through the same ApplyCommand.

     type  fields used
     A, F  side, orderId, shares, price
     E, C  orderId, shares (executed)
     X     orderId, shares (cancelled)
     D     orderId
     U     orderId (old), newOrderId, shares, price
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "book.h"

struct Command {
	char     type;
	Side     side;
	OrderId  orderId;
	OrderId  newOrderId;
	Quantity shares;
	Price    price;
};

/* ParseCommand
   Decodes one ITCH message (starting at its type byte) into a Command.
   Returns nullopt for a truncated message or a type the book does not
   handle.
*/
std::optional<Command> ParseCommand(const uint8_t* data, size_t length);

/* Outcome
   What applying a command did:
   - status: the snapshot status bits it raises (SNAP_STATUS_* in
     snapshot.h -- order_dropped, price_rejected, unknown_order_ref)
   - accepted: the book took the command as intended
   - reason: short trace tag when not accepted, else nullptr
*/
struct Outcome {
	uint8_t     status;
	bool        accepted;
	const char* reason;
};

/* ApplyCommand
   Applies a Command to the book. An unsupported type is a caller bug
   (ParseCommand never produces one) and is reported as not accepted.
*/
Outcome ApplyCommand(Book* book, const Command& cmd);
