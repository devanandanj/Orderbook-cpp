
#include <iostream>

#include "../include/using.h"
#include "../include/orderbook.h"




Order MakeOrder(OrderId orderId, Side side, Price price, Quantity quantity) {
	return Order{ orderId, side, price, quantity };
}

OrderModify MakeOrderModify(OrderId OldOrderId, OrderId NewOrderId, Side side, Price price, Quantity quantity) {
	return OrderModify{ OldOrderId,NewOrderId, side, price, quantity };
}

bool isBuyOrder(const Order& order) {
	return order.side == Side::Buy;
}

int main()
{
	Order o = MakeOrder(1, Side::Buy, 100, 10);

	std::cout << "Order" << o.orderId << "\nPrice" << o.price << "\nQuantity" << o.quantity << std::endl;
	
	return 0;
}