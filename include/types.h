#pragma once
#include <cstdint>
#include "using.h"

enum class Side : uint8_t {
    Buy,
    Sell
};

struct Order {
    OrderId  orderId;
    Side     side;
    Price    price;
    Quantity quantity;
};

struct OrderModify {
    OrderId  oldOrderId_;
    OrderId  newOrderId_;
    Side     side_;
    Price    price_;
    Quantity quantity_;   // Execute (E) not implemented — no partial fill tracking
};

struct LevelInfo {
    Price    price_;
    Quantity quantity_;
};