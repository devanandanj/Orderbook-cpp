/*
   orderbook.cpp
   ----------------
   Orderbook implementation.

*/

#include "../include/orderbook.h"
#include "../include/types.h"

/* FindOrderIndex
   Helper that searches the provided orders array for orderId and
   returns the index or -1 when not found.
*/
static int FindOrderIndex(const Order* orders, uint8_t count, OrderId orderId) {
    for (uint8_t i = 0; i < count; i++) {
        if (orders[i].orderId == orderId)
            return i;
    }
    return -1;
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

static int FindWorstIndex(const Order* orders, uint8_t count, Side side) {
    if (count == 0) return -1;

    int worstIndex{ 0 };
    for (uint8_t i = 0; i < count; i++)
    {
        bool isWorse;
        if (side == Side::Buy)
        {
            isWorse = (orders[i].price < orders[worstIndex].price) ||
                (orders[i].price == orders[worstIndex].price) &&
                (orders[i].orderId > orders[worstIndex].orderId);
        }
        else
        {
            isWorse = (orders[i].price > orders[worstIndex].price) ||
                (orders[i].price == orders[worstIndex].price) &&
                (orders[i].orderId > orders[worstIndex].orderId);
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

static bool IsMoreCompetitive(const Order& incoming, const Order& resting, Side side) {
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

    if (order.side == Side::Buy) {
        arr = orderbook->bids;
        count = &orderbook->bid_count;
    }
    else {
        arr = orderbook->asks;
        count = &orderbook->ask_count;
    }
    if (count >= 32)
    {
        arr[*count] = order;
        (*count)++;
        return AddResult::Inserted;
    }
    /* Side is full -- find the worst resting order and decide whether
        the incoming order is competitive enough to evict it. */

    int worstIndex = FindOrderIndex(arr, count*, order.side);
    if (worstIndex == -1)
    {
        /* Unreachable in practice(count == 32 implies at least one
            entry), guarded defensively.*/
        return AddResult::Discarded;
    }
    if (IsMoreCompetitive(order, arr[worstIndex], order.side))
    {
        arr[worstIndex] = order;
        return AddResult::Evicted;
    }
}

/* CancelOrder
   Remove an order by replacing it with the last element in the array
   and decrementing the count.
*/
bool CancelOrder(Orderbook* orderbook, OrderId orderId) {
    int idx = FindOrderIndex(orderbook->bids, orderbook->bid_count, orderId);
    if (idx != -1) {
        orderbook->bids[idx] = orderbook->bids[orderbook->bid_count - 1];
        orderbook->bid_count--;
        return true;
    }

    idx = FindOrderIndex(orderbook->asks, orderbook->ask_count, orderId);
    if (idx != -1) {
        orderbook->asks[idx] = orderbook->asks[orderbook->ask_count - 1];
        orderbook->ask_count--;
        return true;
    }

    return false;
}

/* ModifyOrder
   Implement a replace by canceling the old order (if present) and
   adding the new order.
*/
ModifyResult ModifyOrder(Orderbook* orderbook, const OrderModify& mod) {
    bool found = CancelOrder(orderbook, mod.oldOrderId);
    if (!found) {
        return ModifyResult::NotFound;
    }

    Order new_order;
    new_order.orderId = mod.newOrderId;
    new_order.side = mod.side;
    new_order.price = mod.price;
    new_order.quantity = mod.quantity;

    switch (AddOrder(orderbook, new_order))
    {
    case AddResult::Inserted:  return ModifyResult::Replaced;
    case AddResult::Evicted:  return ModifyResult::Evicted;
    case AddResult::Discarded: return ModifyResult::Discarded;
    }
    return ModifyResult::Discarded; // unreachable, defensive fallback
}

/* ExecuteOrder
   Apply an execution against an order id. If the executed quantity is
   greater than or equal to the resting quantity the order is removed;
   otherwise the quantity is reduced.
*/
bool ExecuteOrder(Orderbook* orderbook, const OrderExecute& exec) {
    // bids
    int idx = FindOrderIndex(orderbook->bids, orderbook->bid_count, exec.orderId);
    if (idx != -1)
    {
        if (exec.executedQuantity >= orderbook->bids[idx].quantity)
        {
            // fully filled - removed from orderbook
            orderbook->bids[idx] = orderbook->bids[orderbook->bid_count - 1];
            orderbook->bid_count--;
        }
        else
        {
            orderbook->bids[idx].quantity -= exec.executedQuantity;
        }
        return true;
    }
    // asks
    idx = FindOrderIndex(orderbook->asks, orderbook->ask_count, exec.orderId);
    if (idx != -1) {
        if (exec.executedQuantity >= orderbook->asks[idx].quantity) {
            orderbook->asks[idx] = orderbook->asks[orderbook->ask_count - 1];
            orderbook->ask_count--;
        }
        else {
            orderbook->asks[idx].quantity -= exec.executedQuantity;
        }
        return true;
    }

    return false;   // order not found
}
