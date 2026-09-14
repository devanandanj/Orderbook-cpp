
/*
   orderbook.h
   ----------------
   Simple in-memory orderbook representation used by the example code.
   This implementation is intentionally compact for teaching purposes:
   - bids and asks are stored in fixed-size arrays of MAX_ORDERS_PER_SIDE
     entries each (see using.h)
   - there is no price-level aggregation or sorted data structure

   This file declares the Orderbook structure and a small set
   of functions for manipulating it. The implementation lives in
   src/orderbook.cpp.
*/

#pragma once

#include "../include/using.h"
#include "../include/types.h"

/* Orderbook
   - bids: array holding active buy orders
   - asks: array holding active sell orders
   - bid_count / ask_count: number of valid entries in each array
   Note: arrays are fixed-size; callers must tolerate AddOrder failing
   silently when capacity is exhausted.
*/
struct Orderbook {
	Order bids[MAX_ORDERS_PER_SIDE]{};
	Order asks[MAX_ORDERS_PER_SIDE]{};
	Bids bid_count{};
	Asks ask_count{};
    uint64_t bid_reject_book_full{};
    uint64_t ask_reject_book_full{};
};

/* AddOrder
   Inserts a new order into the book. If the corresponding side's array
   is full, evicts the worst resting order if the incoming order is more
   competitive; otherwise returns Discarded and leaves the book unchanged.
   Callers must check the returned AddResult — this does not fail silently.
*/
enum class AddResult : uint8_t
{
	Inserted,	// normal insert, side had room
	Evicted,	// side was full; new order was more competitive, replaced the worst resting order
	Discarded	// side was full; new order was not competitive enough, book unchanged
};

AddResult AddOrder(Orderbook* orderbook, const Order& order);

/* CancelOrder
   Removes an order by order id if present. Returns true when an order
   was found and removed, false otherwise.
*/
bool CancelOrder(Orderbook* orderbook, OrderId order);

/* ModifyOrder
   Apply a replace/modify: remove the old order (if present) and add the
   new one. The caller must populate an OrderModify describing the change.
*/
enum class ModifyResult : uint8_t
{
	Replaced,
	Evicted,
	Discarded,
	NotFound
};

ModifyResult ModifyOrder(Orderbook* orderbook, const OrderModify& mod);

/* ExecuteOrder
   Apply an execution against an order id. If the executed quantity fills
   the order it is removed; otherwise the order's quantity is reduced.
   Returns true if the order was found and updated, false if not found.
*/
bool ExecuteOrder(Orderbook* orderbook, const OrderExecute& exec);

/* FullBookSnapshot
   Serializes full depth of both sides (price, quantity, orderId per resting
   order) into a caller-provided buffer for EOD diff against the FPGA.
   Order within each side is NOT guaranteed to be price-sorted in the
   underlying array — this function sorts before writing so the trace
   format is deterministic and comparable byte-for-byte.
*/
struct BookLevel {
    Price price;
    Quantity quantity;
    OrderId orderId;
};

struct BookSnapshot{
    BookLevel bids[MAX_ORDERS_PER_SIDE]{};
    BookLevel asks[MAX_ORDERS_PER_SIDE]{};
    Bids bid_count{};
    Asks ask_count{};
};

void FullBookSnapshot(const Orderbook* orderbook, BookSnapshot* snapshot);