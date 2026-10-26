#include "../include/trace.h"

static char SideChar(const Side side) {
    return side == Side::Buy ? 'B' : 'S';
}

static void WriteEntryFields(std::ostream& out, const TableEntry& e) {
    out << e.orderId << ',' << SideChar(e.side) << ',' << e.slot << ',' << e.shares;
}

void WriteTraceHeader(std::ostream& out, const Book& book) {
    out << "BOOK," << book.basePrice << ',' << PRICE_TICK << ',' << SLOT_CENTER << ',' << SLOTS << "\n\n";
}

void WriteTraceEntry(std::ostream& out, const size_t msgIndex, const char msgType, const OrderId orderId,
    const bool accepted, const char* reason, const Book& book)
{
    out << "MSG," << msgIndex << ',' << msgType << ',' << orderId << ','
        << (accepted ? "OK" : "REJECTED") << ','
        << (reason ? reason : "") << '\n';

    for (uint32_t i = 0; i < book.log.count; i++)
    {
        const WriteRecord& r = book.log.records[i];
        switch (r.target)
        {
        case WriteTarget::Table:
        case WriteTarget::Cam:
            out << "W," << (r.target == WriteTarget::Table ? "TBL," : "CAM,") << r.index << ','
                << (r.entry.valid ? 1 : 0) << ',';
            WriteEntryFields(out, r.entry);
            out << '\n';
            break;
        case WriteTarget::Bid:
        case WriteTarget::Ask:
            out << "W," << (r.target == WriteTarget::Bid ? "BID," : "ASK,") << r.index << ','
                << r.level.shares << ',' << r.level.orders << '\n';
            break;
        }
    }

    const Bbo bbo = GetBbo(book);
    out << "BBO," << bbo.bidPrice << ',' << bbo.bidShares << ',' << bbo.bidOrders << ','
        << bbo.askPrice << ',' << bbo.askShares << ',' << bbo.askOrders << '\n';
    out << "END\n\n";
}

static void WriteLevels(std::ostream& out, const char* tag, const PriceBook& pb, const Price basePrice) {
    for (uint32_t s = 0; s < SLOTS; s++)
    {
        const Level& l = pb.levels[s];
        if (l.orders == 0) continue;
        out << tag << ',' << s << ',' << SlotToPrice(basePrice, static_cast<Slot>(s)) << ','
            << l.shares << ',' << l.orders << '\n';
    }
}

void WriteStateDump(std::ostream& out, const Book& book) {
    for (uint32_t set = 0; set < SETS; set++)
        for (uint32_t w = 0; w < WAYS; w++)
        {
            const TableEntry& e = book.table.ways[set][w];
            if (!e.valid) continue;
            out << "TBL," << set * WAYS + w << ',';
            WriteEntryFields(out, e);
            out << '\n';
        }
    for (uint32_t i = 0; i < CAM_DEPTH; i++)
    {
        const TableEntry& e = book.table.cam[i];
        if (!e.valid) continue;
        out << "CAM," << i << ',';
        WriteEntryFields(out, e);
        out << '\n';
    }
    WriteLevels(out, "BID", book.bids, book.basePrice);
    WriteLevels(out, "ASK", book.asks, book.basePrice);
}
