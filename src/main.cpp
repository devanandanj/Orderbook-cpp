
/*
   main.cpp
   ----------------
   Reads a MoldUDP64-framed file, de frames the
   envelope into inner messages, parses each message and applies it
   against an in-memory Orderbook.

   Outputs:
   - snapshot.bin: two SNAP_PACKET_LEN packets (bids, asks) per message,
     byte-identical to what the RTL streams. This is the scoreboard trace.
   - trace.txt: human-readable dump of the same state.

   A D/U/E against an order that is not resting is NOT an error: the book
   is bounded, so the feed will reference orders it evicted or discarded.
   Like the RTL, the model raises unknown_order_ref for that message,
   leaves the book unchanged and carries on.

*/

#include <iostream>
#include <fstream>
#include <filesystem>

#include "../include/using.h"
#include "../include/orderbook.h"
#include "../include/moldudp64.h"
#include "../include/itchparser.h"
#include "../include/trace.h"
#include "../include/latencystats.h"
#include "../include/snapshot.h"

/* read_file_bytes
   Read the entire file into a vector<uint8_t>. Returns an empty vector on
   failure. This helper is synchronous and loads the whole file into memory
   which is fine for small test files used by this project.
*/
static std::vector<uint8_t> read_file_bytes(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open: " << std::filesystem::absolute(path) << "\n";
		return {};
	}
	const std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
	{
		return {};
	}
	return buffer;
}

int main(int argc, char** argv) {
	if (argc != 2 && argc != 3)
	{
		std::cerr << "Usage: " << argv[0] << " <MoldUDP64_file> [snapshot_out]" << std::endl;
		return 1;
	}

	std::vector<uint8_t> buf = read_file_bytes(argv[1]);
	if (buf.empty()) {
		std::cerr << "Failed to read file: " << argv[1] << std::endl;
		return 1;
	}

	std::vector<MoldUDPMessage> messages = deframe_moldudp64(buf.data(), buf.size());
	if (messages.empty())
	{
		std::cerr << "De-framing failed or produced no messages -- aborting." << std::endl;
		return 1;
	}

	// ponytail: file holds one MoldUDP64 packet, so no gap detection and
	// mold_gap_seen stays 0. Add per-packet seq tracking with multi-packet input.
	const uint64_t mold_seq = read_u64_be(buf.data(), MOLDUDP64_SEQNUM_OFFSET);

	Orderbook book = {};
	std::ofstream trace(std::string(PROJECT_ROOT) + "/trace.txt");
	const std::string snapshot_path = argc == 3 ? argv[2] : std::string(PROJECT_ROOT) + "/snapshot.bin";
	std::ofstream snapshot(snapshot_path, std::ios::binary);
	if (!snapshot)
	{
		std::cerr << "Failed to open " << snapshot_path << " for writing." << std::endl;
		return 1;
	}
	uint32_t snap_seq = 0;
	uint64_t unknown_order_refs = 0;
	LatencyStats parse_stats;
	LatencyStats apply_stats;
	// Prevent a vector resize inside a timing window -- a mid-loop
	// reallocation shows up as an outlier that wasn't really there.
	parse_stats.reserve(messages.size());
	apply_stats.reserve(messages.size());

	for (size_t i = 0; i < messages.size(); i++)
	{
		const auto&[data, length] = messages[i];
		if (length == 0)
		{
			std::cerr << "Message " << i << ": zero length -- nothing to dispatch" << std::endl;
			return 1;
		}

		bool unknown_ref = false;
		switch (uint8_t msg_type = data[0]) {
		case 'A': {

			auto t0 = std::chrono::steady_clock::now();
			auto order = parse_add(data, length, 0);
			auto t1 = std::chrono::steady_clock::now();
			parse_stats.record(t1 - t0);

			if (!order.has_value())
			{
				WriteTraceEntry(trace, i, 'A', 0, false, "malformed", book);
				std::cerr << "Message " << i << ": malformed 'A' message -- aborting." << std::endl;
				return 1;
			}
			auto t2 = std::chrono::steady_clock::now();
			AddResult result = AddOrder(&book, *order);
			auto t3 = std::chrono::steady_clock::now();
			apply_stats.record(t3 - t2);
			
			const char* reason = nullptr;
			if (result == AddResult::Evicted) reason = "Evicted worst order";
			if (result == AddResult::Discarded) reason = "Book full order not competitive";
			bool accepted = (result != AddResult::Discarded);

			WriteTraceEntry(trace, i, 'A', order->orderId, accepted, reason, book);
			
			if (!accepted)
			{
				std::clog << "Message" << i <<
					": AddOrder failed (book full) for Order ID = "
					<< (unsigned long long)order->orderId << std::endl;
			}
			break;
		}
		case 'D': {
			auto t0 = std::chrono::steady_clock::now();
			auto orderId = parse_delete(data, length, 0);
			auto t1 = std::chrono::steady_clock::now();
			parse_stats.record(t1 - t0);

			if (!orderId.has_value())
			{
				WriteTraceEntry(trace, i, 'D', 0, false, "Malformed", book);
				std::cerr << "Message " << i << ": malformed 'D' message -- aborting." << std::endl;
				return 1;
			}

			auto t2 = std::chrono::steady_clock::now();
			bool result = CancelOrder(&book, *orderId);
			auto t3 = std::chrono::steady_clock::now();
			apply_stats.record(t3 - t2);

			unknown_ref = !result;
			WriteTraceEntry(trace, i, 'D', *orderId, result, result ? nullptr : "not_found", book);
			break;
		}
		case 'U': {

			auto t0 = std::chrono::steady_clock::now();
			auto fields = parse_replace(data, length, 0);
			auto t1 = std::chrono::steady_clock::now();
			parse_stats.record(t1 - t0);

			if (!fields.has_value())
			{	
				WriteTraceEntry(trace, i, 'U', 0, false, "malformed", book);
				std::cerr << "Message " << i << ": malformed 'U' message -- aborting." << std::endl;
				return 1;
			}
			OrderModify mod{ fields->OldOrderId, fields->NewOrderId, fields->price, fields->quantity };

			auto t2 = std::chrono::steady_clock::now();
			ModifyResult result = ModifyOrder(&book, mod);
			auto t3 = std::chrono::steady_clock::now();
			apply_stats.record(t3 - t2);

			// Old order is removed first, so the side always has room:
			// Replaced or NotFound are the only reachable outcomes.
			unknown_ref = result == ModifyResult::NotFound;
			WriteTraceEntry(trace, i, 'U', fields->NewOrderId, !unknown_ref,
				unknown_ref ? "old_id_not_found" : nullptr, book);
			break;
		}
		case 'E': {

			auto t0 = std::chrono::steady_clock::now();
			auto exec = parse_execute(data, length, 0);
			auto t1 = std::chrono::steady_clock::now();
			parse_stats.record(t1 - t0);

			if (!exec.has_value())
			{
				WriteTraceEntry(trace, i, 'E', 0, false, "malformed", book);
				std::cerr << "Message " << i 
					<< ": malformed 'E' message -- aborting." << std::endl;
				return 1;
			}
			OrderExecute order_exec{ exec->orderId, exec->executedQuantity, exec->matchId };

			auto t2 = std::chrono::steady_clock::now();
			bool result = ExecuteOrder(&book, order_exec);
			auto t3 = std::chrono::steady_clock::now();
			apply_stats.record(t3 - t2);

			unknown_ref = !result;
			WriteTraceEntry(trace, i, 'E', exec->orderId, result, result ? nullptr : "not_found", book);
			break;
		}
		default: {
			std::cerr << "Message " << i 
				<< ": unknown message type'" << msg_type << "' -- aborting." << std::endl;
			return 1;
		}
		}

		unknown_order_refs += unknown_ref;
		const uint8_t status = unknown_ref ? 1u << SNAP_STATUS_UNKNOWN_ORDER_REF : 0;
		for (const Side side : { Side::Buy, Side::Sell })
		{
			uint8_t pkt[SNAP_PACKET_LEN];
			EncodeSnapshotPacket(pkt, book, side, status, snap_seq++, mold_seq, static_cast<uint32_t>(i));
			snapshot.write(reinterpret_cast<const char*>(pkt), SNAP_PACKET_LEN);
		}
	}

	std::cout << "Processed " << messages.size() << " messages successfully." << std::endl;
	std::cout << "Final book state: bids=" 
		<< static_cast<int>(book.bid_count) << " asks=" << static_cast<int>(book.ask_count) << std::endl;
	std::cout << "Rejects (book full): bid=" << book.bid_reject_book_full
		<< " ask=" << book.ask_reject_book_full
		<< " unknown_order_ref=" << unknown_order_refs << std::endl;
	std::cout << "Wrote " << snap_seq << " snapshot packets to " << snapshot_path << std::endl;

	// Measure and subtract the observer's own cost so the printed
	// numbers are the work, not the work + clock reads.
	const auto timer_overhead = LatencyStats::estimate_timer_overhead();
	parse_stats.report("parse", timer_overhead);
	apply_stats.report("apply", timer_overhead);

	return 0;
}
