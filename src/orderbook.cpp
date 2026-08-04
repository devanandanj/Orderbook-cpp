#include "../include/orderbook.h"
#include "../include/types.h"

static int FindOrderIndex(const Order* orders, uint8_t count, OrderId orderId) {
    for (uint8_t i = 0; i < count; i++) {
        if (orders[i].orderId == orderId)
            return i;
    }
    return -1;
}

void AddOrder(Orderbook* orderbook, const Order& order) {
    if (order.side == Side::Buy) {
        if (orderbook->bid_count >= 32) return;
        orderbook->bids[orderbook->bid_count] = order;
        orderbook->bid_count++;
    }
    else {
        if (orderbook->ask_count >= 32) return;
        orderbook->asks[orderbook->ask_count] = order;
        orderbook->ask_count++;
    }
}

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

void ModifyOrder(Orderbook* orderbook, const OrderModify& mod) {
    bool found = CancelOrder(orderbook, mod.oldOrderId_);
    if (!found) {
        // TODO: decide — log warning, assert, or silently proceed
    }

    Order new_order;
    new_order.orderId = mod.newOrderId_;
    new_order.side = mod.side_;
    new_order.price = mod.price_;
    new_order.quantity = mod.quantity_;

    AddOrder(orderbook, new_order);
}