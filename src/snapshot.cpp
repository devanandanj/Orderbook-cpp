#include "../include/snapshot.h"

#include <cstring>


static void put_be(uint8_t* p, uint64_t v, const size_t width) {
    for (size_t i = width; i-- > 0; v >>= 8)
        p[i] = static_cast<uint8_t>(v);
}

void EncodeSnapshotPacket(uint8_t* out, const Book& book, const uint8_t status,
    const uint32_t snapSeq, const uint64_t moldSeq, const uint32_t msgIndex)
{
    std::memset(out, 0, SNAP_PACKET_LEN);
    const Bbo bbo = GetBbo(book);

    put_be(out + 0, SNAP_MAGIC, 2);
    out[2] = SNAP_VERSION;
    out[3] = status;
    put_be(out + 4, snapSeq, 4);
    put_be(out + 8, moldSeq, 8);
    put_be(out + 16, msgIndex, 4);
    // [20..27] t_arrive / t_commit: hardware-only, left 0.
    put_be(out + 28, bbo.bidPrice, 4);
    put_be(out + 32, bbo.bidShares, 4);
    put_be(out + 36, bbo.askPrice, 4);
    put_be(out + 40, bbo.askShares, 4);
    put_be(out + 44, bbo.bidOrders, 2);
    put_be(out + 46, bbo.askOrders, 2);
}
