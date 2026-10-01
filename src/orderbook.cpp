/*
   orderbook.cpp
   ----------------
   Orderbook implementation.

*/

#include "../include/orderbook.h"
#include "../include/types.h"
#include <algorithm>

/* FindOrderIndex
   Helper that searches the provided orders array for orderId and
   returns the index or -1 when not found.
*/
static int FindOrderIndex(const Order* orders, const uint8_t count, const OrderId orderId) {
    for (uint8_t i = 0; i < count; i++) {
        if (orders[i].orderId == orderId)
            return i;
    }
    return -1;
}

static void RemoveAt(Order* arr, uint8_t* count, int idx) {
    arr[idx] = arr[(*count) - 1];
    --(*count);
}




/* FindWorstIndex
   Scans one side of the book and returns the index of the least
   competitive resting order:
   - Buy side:  lowest price is worst (furthest from matching)
   - Sell side: highest price is worst (furthest from matching)
   Ties are broken by orderId ascending, i.e. the order that arrived
   earlier (lower orderId, treated as a first-come-first-served proxy
   since Order has no timestamp field) is favored to stay; the later
   arrival is considered "worse" on a tie and is the one eligible for
   eviction.
*/

static int FindWorstIndex(const Order* orders, const uint8_t count, const Side side) {
    if (count == 0) return -1;

    int worstIndex{ 0 };
    for (uint8_t i = 0; i < count; i++)
    {
        bool isWorse;
        if (side == Side::Buy)
        {
            isWorse = (orders[i].price < orders[worstIndex].price) ||
                ((orders[i].price == orders[worstIndex].price) &&
                    (orders[i].orderId > orders[worstIndex].orderId));
        }
        else
        {
            isWorse = (orders[i].price > orders[worstIndex].price) ||
                ((orders[i].price == orders[worstIndex].price) &&
                    (orders[i].orderId > orders[worstIndex].orderId));
        }
        if (isWorse) worstIndex = i;
    }
    return worstIndex;
}

/* IsMoreCompetitive
   Returns true if 'incoming' should displace 'resting' when the book
   is full. On an exact price tie, the incoming order does NOT win --
   first-come-first-served means the existing resting order keeps its
   place.
*/

static bool IsMoreCompetitive(const Order& incoming, const Order& resting, const Side side) {
    if (side == Side::Buy)
    {
        return incoming.price > resting.price;
    }
    else
    {
        return incoming.price < resting.price;
    }
}



/* AddOrder
   Appends the order to the correct side's array if there is capacity.
   If the array is full the function returns without adding the order.
*/
AddResult AddOrder(Orderbook* orderbook, const Order& order) {
    Order* arr;
    uint8_t* count;
    uint64_t* rejectCounter;

    if (order.side == Side::Buy) {
        arr = orderbook->bids;
        count = &orderbook->bid_count;
        rejectCounter = &orderbook->bid_reject_book_full;
    }
    else {
        arr = orderbook->asks;
        count = &orderbook->ask_count;
        rejectCounter = &orderbook->ask_reject_book_full;
    }
    if (*count < MAX_ORDERS_PER_SIDE)
    {
        arr[*count] = order;
        (*count)++;
        return AddResult::Inserted;
    }

    const int worstIndex = FindWorstIndex(arr, *count, order.side);
    if (worstIndex == -1)
    {
        ++(*rejectCounter);
        return AddResult::Discarded;
    }
    if (IsMoreCompetitive(order, arr[worstIndex], order.side))
    {
        arr[worstIndex] = order;
        return AddResult::Evicted;
    }
    ++(*rejectCounter);
    return AddResult::Discarded;
}

/* FindOrder
   Locates an order id on either side. Ids are unique across the whole
   book, so this is the software form of the RTL's single-cycle
   128-wide broadcast compare: one lookup per D/U/E message.
*/
struct OrderSlot {
    Order* arr;
    uint8_t* count;
    int idx;    // -1 when not found
};

static OrderSlot FindOrder(Orderbook* orderbook, const OrderId orderId) {
    int idx = FindOrderIndex(orderbook->bids, orderbook->bid_count, orderId);
    if (idx >= 0) return {orderbook->bids, &orderbook->bid_count, idx};
    idx = FindOrderIndex(orderbook->asks, orderbook->ask_count, orderId);
    if (idx >= 0) return {orderbook->asks, &orderbook->ask_count, idx};
    return {nullptr, nullptr, -1};
}

/* CancelOrder
   Remove an order by replacing it with the last element in the array
   and decrementing the count.
*/
bool CancelOrder(Orderbook* orderbook, OrderId orderId) {
    const OrderSlot s = FindOrder(orderbook, orderId);
    if (s.idx < 0) return false;
    RemoveAt(s.arr, s.count, s.idx);
    return true;
}

/* ModifyOrder
   Implement a replace by removing the old order (if present) and
   adding the new one on the old order's side. The ITCH 'U' message
   carries no side, so it is taken from the resting order -- the same
   lookup that finds it.
*/
ModifyResult ModifyOrder(Orderbook* orderbook, const OrderModify& mod) {
    const OrderSlot s = FindOrder(orderbook, mod.oldOrderId);
    if (s.idx < 0)
        return ModifyResult::NotFound;

    const Side side = s.arr[s.idx].side;
    RemoveAt(s.arr, s.count, s.idx);

    const Order newOrder{.orderId = mod.newOrderId, .side = side, .price = mod.price, .quantity = mod.quantity};
    switch (AddOrder(orderbook, newOrder)) {
        case AddResult::Inserted: return ModifyResult::Replaced;
        case AddResult::Evicted:  return ModifyResult::Evicted;
        default:                  return ModifyResult::Discarded;
    }
}

/* ExecuteOrder
   Apply an execution against an order id. If the executed quantity is
   greater than or equal to the resting quantity the order is removed;
   otherwise the quantity is reduced.
*/
bool ExecuteOrder(Orderbook* orderbook, const OrderExecute& exec) {
    const OrderSlot s = FindOrder(orderbook, exec.orderId);
    if (s.idx < 0) return false;
    if (Order& o = s.arr[s.idx]; exec.executedQuantity >= o.quantity)
        RemoveAt(s.arr, s.count, s.idx);
    else
        o.quantity -= exec.executedQuantity;
    return true;
}

void FullBookSnapshot(const Orderbook *orderbook, BookSnapshot *snapshot) {
    snapshot->bid_count = orderbook->bid_count;
    snapshot->ask_count = orderbook->ask_count;
    for (int i = 0; i < orderbook->bid_count; ++i)
        snapshot->bids[i] = {orderbook->bids[i].price, orderbook->bids[i].quantity, orderbook->bids[i].orderId};
    for (int i = 0; i < orderbook->ask_count; ++i)
        snapshot->asks[i] = {orderbook->asks[i].price, orderbook->asks[i].quantity, orderbook->asks[i].orderId};
    std::sort(snapshot->bids, snapshot->bids + snapshot->bid_count,
        [](const BookLevel& a, const BookLevel& b) {
            return a.price != b.price ? a.price > b.price : a.orderId < b.orderId;
        });
    std::sort(snapshot->asks, snapshot->asks + snapshot->ask_count,
        [](const BookLevel& a, const BookLevel& b) {
            return a.price != b.price ? a.price < b.price : a.orderId < b.orderId;
        });
}
