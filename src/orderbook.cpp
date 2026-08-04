
#include <iostream>
#include <map>
#include <list>
#include "../include/using.h"


enum class Side : uint8_t {
	Buy,
	Sell
};


struct LevelInfos {
	OrderId orderid_;
	Quantity quantity_;
};


struct Order {
	OrderId orderId;
	Side side;
	Price price;
	Quantity quantity;

};

struct OrderModify{
	OrderId orderId_;
	Side side_;
	Price price_;
	Quantity quantity_;		//Execure(E) not implemented, no partial fill tracking.
};

Order MakeOrder(OrderId orderId, Side side, Price price, Quantity quantity) {
	return Order{ orderId, side, price, quantity };
}

OrderModify MakeOrderModify(OrderId orderId, Side side, Price price, Quantity quantity) {
	return OrderModify{ orderId, side, price, quantity };
}

bool isBuyOrder(const Order& order) {
	return order.side == Side::Buy;
}


int main()
{
	Order o = MakeOrder(1, Side::Buy, 100, 10);

	std::cout << "Order" << o.orderId << "Price" << o.price << "Quantity" << o.quantity << std::endl;
	
	return 0;
}