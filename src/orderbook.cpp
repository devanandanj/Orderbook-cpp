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

/* AddOrder
   Appends the order to the correct side's array if there is capacity.
   If the array is full the function returns without adding the order.
*/
bool AddOrder(Orderbook* orderbook, const Order& order) {
    if (order.side == Side::Buy) {
        if (orderbook->bid_count >= 32) return false;
        orderbook->bids[orderbook->bid_count] = order;
        orderbook->bid_count++;
    }
    else {
        if (orderbook->ask_count >= 32) return false;
        orderbook->asks[orderbook->ask_count] = order;
        orderbook->ask_count++;
    }
    return true;
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
bool ModifyOrder(Orderbook* orderbook, const OrderModify& mod) {
    bool found = CancelOrder(orderbook, mod.oldOrderId);
    if (!found) {
        return false;
    }

    Order new_order;
    new_order.orderId = mod.newOrderId;
    new_order.side = mod.side;
    new_order.price = mod.price;
    new_order.quantity = mod.quantity;

    return AddOrder(orderbook, new_order);
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
