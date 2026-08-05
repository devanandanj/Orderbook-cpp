
#pragma once

#include "../include/using.h"
#include "../include/types.h"

struct Orderbook {
	Order bids[32];
	Order asks[32];
	Bids bid_count{};
	Asks ask_count{};

};

void AddOrder(Orderbook* orderbook, const Order& order);
bool CancelOrder(Orderbook* orderbook, OrderId order);
void ModifyOrder(Orderbook* orderbook, const OrderModify& mod);
bool ExecuteOrder(Orderbook* orderbook, const OrderExecute& exec);