/*
   types.h
   ----------------
   Fundamental data types and small POD structures used by the project.
   This header centralizes domain-level typedefs and plain-old-data
   structs so they are easy to find and understand.
*/

#pragma once
#include <cstdint>
#include "using.h"

/* Side
   Simple enum describing order side: Buy or Sell.
*/
enum class Side : uint8_t {
    Buy,
    Sell
};

/* Order
   Represents an active resting order in the book.
*/
struct Order {
    OrderId  orderId;
    Side     side;
    Price    price;
    Quantity quantity;
};

/* OrderModify
   Represents a replace/update instruction: the old order id being
   replaced and the new order fields. Note: partial executions are not
   tracked by this simple example, so quantity is treated as the new
   resting quantity after the replace.
*/
struct OrderModify {
    OrderId  oldOrderId_;
    OrderId  newOrderId_;
    Side     side_;
    Price    price_;
    Quantity quantity_;
};

/* OrderExecute
   Represents an execution report from the feed. Fields:
   - orderId: resting order that was executed
   - executedQuantity: how many units were executed
   - matchId: optional match identifier (provided by the feed)
*/
struct OrderExecute{
    OrderId orderId;
    Quantity executedQuantity;
    OrderId matchId;
};
