/*
   main.cpp
   ----------------
   Reads a file of MoldUDP64 packets laid back to back (one packet, as from
   test/gen_test.cpp, or many, as from tools/itch_to_mold.py), and for each
   book message: parses it into a Command, applies it to the book, and
   emits the snapshot packet.

   Usage: Orderbook-cpp <mold_file> [snapshot_out] [--base-price <p>] [--symbol <SYM>]
                        [--trace <path>] [--state <path>]

   Which messages reach the book:
   - Only the book types A F E C X D U. Every other type (system events,
     stock directory, trading actions, hidden-order trades 'P', RPII 'N',
     ...) is skipped: no book change, no snapshot packet, no trace entry.
     msg_index still counts every message in the stream, so skipped ones
     show as gaps in the snapshots' msg_index.
   - With --symbol, only that symbol's book messages. Its stock locate is
     learned from the stream's 'R' (stock directory) message for it. The
     RTL does the same with a locate register the host loads.
   Without --symbol every book message is applied to the one book; a
   warning is printed if they come from more than one stock locate.

   The base price anchors the price window (slot SLOT_CENTER). It must be
   on the PRICE_TICK grid; without --base-price it defaults to the first
   selected add's price rounded down to the tick. The RTL takes the same
   value from a register, so a run is only comparable to an RTL run with
   the same base.

   MoldUDP64: a packet whose sequence number is not the one expected (the
   previous packet's plus its message count) sets mold_gap_seen, sticky,
   in every later snapshot. Heartbeats carry the next expected sequence
   and are checked the same way; end-of-session stops the run.

   Outputs (default location: the repo root; --trace / --state move them):
   - snapshot.bin: one SNAP_PACKET_LEN BBO packet per applied message,
     byte-identical to what the RTL streams. This is the scoreboard trace.
   - trace.txt: a BOOK geometry line, then per applied message: outcome,
     memory writes and BBO. tools/show_book.py rebuilds the full price
     ladder at any message from it.
   - state.txt: end-of-run dump of every table entry and occupied level,
     for comparison against a readback of the RTL's memories.

   A D/U/E/C/X against an order that is not in the table is NOT an error:
   the table drops orders when a set and the CAM are full and rejects
   out-of-window prices, so the feed will reference orders the book never
   held. Like the RTL, the model raises unknown_order_ref for that message,
   leaves the book unchanged and carries on.

*/

#include <algorithm>
#include <charconv>
#include <cstring>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

#include "../include/using.h"
#include "../include/book.h"
#include "../include/command.h"
#include "../include/moldudp64.h"
#include "../include/itchparser.h"
#include "../include/trace.h"
#include "../include/latencystats.h"
#include "../include/snapshot.h"

/* read_file_bytes
   Read the entire file into a vector<uint8_t>. Returns an empty vector on
   failure. The whole file is held in memory: fine for a single symbol's
   day (SPY on BX is ~6 MB), heavy for an unfiltered full-day file.
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

static bool IsBookType(const uint8_t type) {
	return std::memchr("AFECXDU", type, 7) != nullptr;
}

/* Selector
   Decides which messages reach the book (see the header comment). Feed it
   every message in stream order: it learns the symbol's locate from the
   'R' message as that goes by.
*/
struct Selector {
	bool     bySymbol = false;
	char     symbol[8]{};          // space-padded, as in the 'R' message
	bool     locateKnown = false;
	uint16_t locate = 0;

	bool Selects(const uint8_t* data, const uint16_t length) {
		if (length < 3) return false;
		const uint16_t msgLocate = read_u16_be(data, 1);
		if (bySymbol && data[0] == 'R' && length >= 19 && std::memcmp(data + 11, symbol, 8) == 0)
		{
			locate = msgLocate;
			locateKnown = true;
		}
		if (!IsBookType(data[0])) return false;
		return !bySymbol || (locateKnown && msgLocate == locate);
	}
};

/* ForEachPacket
   Walks the back-to-back packets, calling fn(packet) for each. Stops after
   end-of-session. Returns false (with a message) on a malformed packet.
*/
template <class Fn>
static bool ForEachPacket(const std::vector<uint8_t>& buf, Fn&& fn) {
	size_t off = 0;
	while (off < buf.size())
	{
		const std::optional<MoldPacket> packet = next_moldudp64_packet(buf.data() + off, buf.size() - off);
		if (!packet)
		{
			std::cerr << "Malformed MoldUDP64 packet at byte " << off << " -- aborting." << std::endl;
			return false;
		}
		off += packet->length;
		fn(*packet);
		if (packet->count == MOLDUDP64_END_OF_SESSION) break;
	}
	return true;
}

/* DefaultBasePrice
   The first selected add's price rounded down to the tick, or 0 if there
   is none. Only used when --base-price is not given.
*/
static std::optional<Price> DefaultBasePrice(const std::vector<uint8_t>& buf, Selector selector) {
	Price base = 0;
	bool found = false;
	const bool ok = ForEachPacket(buf, [&](const MoldPacket& packet) {
		for (const auto& [data, length] : packet.messages)
		{
			if (found || !selector.Selects(data, length)) continue;
			const std::optional<Command> cmd = ParseCommand(data, length);
			if (cmd && (cmd->type == 'A' || cmd->type == 'F'))
			{
				base = cmd->price - cmd->price % PRICE_TICK;
				found = true;
			}
		}
	});
	if (!ok) return std::nullopt;
	return base;
}

static Book book;   // ~340 KB: static, not on the stack
static bool locate_seen[1u << 16];

int main(int argc, char** argv) {
	const char* input = nullptr;
	const char* snapshot_arg = nullptr;
	std::string trace_path = std::string(PROJECT_ROOT) + "/trace.txt";
	std::string state_path = std::string(PROJECT_ROOT) + "/state.txt";
	std::optional<Price> base_arg;
	Selector selector;
	bool bad_args = false;
	for (int a = 1; a < argc; a++)
	{
		const std::string arg = argv[a];
		if (arg == "--base-price" && a + 1 < argc)
		{
			const std::string value = argv[++a];
			Price p{};
			const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), p);
			if (ec != std::errc{} || end != value.data() + value.size()) bad_args = true;
			base_arg = p;
		}
		else if (arg == "--symbol" && a + 1 < argc)
		{
			const std::string sym = argv[++a];
			if (sym.empty() || sym.size() > 8) bad_args = true;
			std::memset(selector.symbol, ' ', 8);
			std::memcpy(selector.symbol, sym.data(), std::min<size_t>(sym.size(), 8));
			selector.bySymbol = true;
		}
		else if (arg == "--trace" && a + 1 < argc)
			trace_path = argv[++a];
		else if (arg == "--state" && a + 1 < argc)
			state_path = argv[++a];
		else if (!input)
			input = argv[a];
		else if (!snapshot_arg)
			snapshot_arg = argv[a];
		else
			bad_args = true;
	}
	if (!input || bad_args)
	{
		std::cerr << "Usage: " << argv[0]
			<< " <mold_file> [snapshot_out] [--base-price <price>] [--symbol <SYM>]"
			   " [--trace <path>] [--state <path>]" << std::endl;
		return 1;
	}

	std::vector<uint8_t> buf = read_file_bytes(input);
	if (buf.empty()) {
		std::cerr << "Failed to read file: " << input << std::endl;
		return 1;
	}

	const std::optional<Price> default_base = base_arg ? base_arg : DefaultBasePrice(buf, selector);
	if (!default_base) return 1;
	const Price base_price = *default_base;
	if (base_price % PRICE_TICK != 0)
	{
		std::cerr << "Base price " << base_price << " is not a multiple of the tick ("
			<< PRICE_TICK << ") -- aborting." << std::endl;
		return 1;
	}

	ResetBook(&book, base_price);
	std::ofstream trace(trace_path);
	WriteTraceHeader(trace, book);
	const std::string snapshot_path = snapshot_arg ? snapshot_arg : std::string(PROJECT_ROOT) + "/snapshot.bin";
	std::ofstream snapshot(snapshot_path, std::ios::binary);
	if (!snapshot)
	{
		std::cerr << "Failed to open " << snapshot_path << " for writing." << std::endl;
		return 1;
	}

	uint32_t snap_seq = 0;
	uint64_t unknown_order_refs = 0;
	uint64_t packets = 0, heartbeats = 0, gaps = 0, skipped = 0, applied = 0;
	uint32_t locates = 0;
	size_t msg_index = 0;
	bool first_packet = true, malformed = false;
	uint64_t expected_seq = 0;
	uint8_t sticky_status = 0;   // mold_gap_seen, once set
	LatencyStats parse_stats;
	LatencyStats apply_stats;
	// Prevent a vector resize inside a timing window -- a mid-loop
	// reallocation shows up as an outlier that wasn't really there. One
	// sample per SELECTED message: on an unfiltered day with --symbol, sizing
	// by every message would reserve ~460 MB for a few hundred thousand.
	size_t selected_messages = 0;
	Selector counter = selector;   // fresh copy: learns the locate on its own
	ForEachPacket(buf, [&](const MoldPacket& p) {
		for (const auto& [data, length] : p.messages)
			selected_messages += counter.Selects(data, length);
	});
	parse_stats.reserve(selected_messages);
	apply_stats.reserve(selected_messages);

	const bool ok = ForEachPacket(buf, [&](const MoldPacket& packet) {
		if (malformed) return;
		packets++;
		if (packet.count == MOLDUDP64_END_OF_SESSION) return;
		if (!first_packet && packet.sequence != expected_seq)
		{
			gaps++;
			sticky_status |= 1u << SNAP_STATUS_MOLD_GAP_SEEN;
		}
		first_packet = false;
		if (packet.count == MOLDUDP64_HEARTBEAT)
		{
			heartbeats++;
			expected_seq = packet.sequence;
			return;
		}
		expected_seq = packet.sequence + packet.count;

		for (const auto& [data, length] : packet.messages)
		{
			const size_t i = msg_index++;
			if (!selector.Selects(data, length))
			{
				skipped++;
				continue;
			}
			if (!selector.bySymbol && !locate_seen[read_u16_be(data, 1)])
			{
				locate_seen[read_u16_be(data, 1)] = true;
				locates++;
			}

			auto t0 = std::chrono::steady_clock::now();
			const std::optional<Command> cmd = ParseCommand(data, length);
			auto t1 = std::chrono::steady_clock::now();
			parse_stats.record(t1 - t0);

			if (!cmd)
			{
				std::cerr << "Message " << i << ": malformed '" << static_cast<char>(data[0])
					<< "' message (" << length << " bytes) -- aborting." << std::endl;
				malformed = true;
				return;
			}

			auto t2 = std::chrono::steady_clock::now();
			const Outcome outcome = ApplyCommand(&book, *cmd);
			auto t3 = std::chrono::steady_clock::now();
			apply_stats.record(t3 - t2);
			applied++;

			unknown_order_refs += (outcome.status >> SNAP_STATUS_UNKNOWN_ORDER_REF) & 1u;

			// For a replace the trace names the new ORN -- the one now resting.
			const OrderId traced_id = cmd->type == 'U' ? cmd->newOrderId : cmd->orderId;
			WriteTraceEntry(trace, i, cmd->type, traced_id, outcome.accepted, outcome.reason, book);

			uint8_t pkt[SNAP_PACKET_LEN];
			EncodeSnapshotPacket(pkt, book, outcome.status | sticky_status, snap_seq++,
				packet.sequence, static_cast<uint32_t>(i));
			snapshot.write(reinterpret_cast<const char*>(pkt), SNAP_PACKET_LEN);
		}
	});
	if (!ok || malformed) return 1;

	if (selector.bySymbol && !selector.locateKnown)
	{
		std::cerr << "Symbol '" << std::string(selector.symbol, 8)
			<< "' has no 'R' stock-directory message in the stream -- nothing applied." << std::endl;
		return 1;
	}

	std::ofstream state(state_path);
	WriteStateDump(state, book);

	// A truncated snapshot.bin (e.g. disk full) would read as an RTL
	// mismatch, so a failed write must fail the run, not report success.
	snapshot.flush();
	trace.flush();
	state.flush();
	if (!snapshot || !trace || !state)
	{
		std::cerr << "Failed writing " << (!snapshot ? snapshot_path : !trace ? trace_path : state_path)
			<< " (disk full or not writable) -- outputs are incomplete." << std::endl;
		return 1;
	}

	const Bbo bbo = GetBbo(book);
	std::cout << "Processed " << msg_index << " messages successfully." << std::endl;
	std::cout << "Packets: " << packets << " (" << heartbeats << " heartbeats), sequence gaps: " << gaps
		<< "; applied " << applied << ", skipped " << skipped << std::endl;
	if (selector.bySymbol)
		std::cout << "Symbol: " << std::string(selector.symbol, 8) << " (locate " << selector.locate << ")" << std::endl;
	else if (locates > 1)
		std::cout << "WARNING: book messages from " << locates
			<< " stock locates went into one book -- use --symbol" << std::endl;
	std::cout << "Base price: " << base_price << std::endl;
	std::cout << "Final BBO: bid=" << bbo.bidPrice << " x " << bbo.bidShares
		<< " (" << bbo.bidOrders << " orders) ask=" << bbo.askPrice << " x " << bbo.askShares
		<< " (" << bbo.askOrders << " orders)" << std::endl;
	std::cout << "Counters: dropped=" << book.dropped
		<< " price_rejected=" << book.priceRejected
		<< " unknown_order_ref=" << unknown_order_refs << std::endl;
	std::cout << "Wrote " << snap_seq << " snapshot packets to " << snapshot_path << std::endl;

	// Measure and subtract the observer's own cost so the printed
	// numbers are the work, not the work + clock reads.
	const auto timer_overhead = LatencyStats::estimate_timer_overhead();
	parse_stats.report("parse", timer_overhead);
	apply_stats.report("apply", timer_overhead);

	return 0;
}
