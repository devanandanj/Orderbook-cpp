#include <iostream>
#include "../include/orderbook.h"

static void PrintBook(const Orderbook& book) {
    std::cout << "  bid_count=" << (int)book.bid_count
        << " ask_count=" << (int)book.ask_count << "\n";
    for (int i = 0; i < book.bid_count; i++) {
        std::cout << "    bid[" << i << "] id=" << book.bids[i].orderId
            << " price=" << book.bids[i].price
            << " qty=" << book.bids[i].quantity << "\n";
    }
    for (int i = 0; i < book.ask_count; i++) {
        std::cout << "    ask[" << i << "] id=" << book.asks[i].orderId
            << " price=" << book.asks[i].price
            << " qty=" << book.asks[i].quantity << "\n";
    }
}

int main() {
    Orderbook book{};

    std::cout << "Step 1: Add 101 Buy 100/50\n";
    Order o1{ 101, Side::Buy, 100, 50 };
    AddOrder(&book, o1);
    PrintBook(book);

    std::cout << "Step 2: Add 102 Buy 101/30\n";
    Order o2{ 102, Side::Buy, 101, 30 };
    AddOrder(&book, o2);
    PrintBook(book);

    std::cout << "Step 3: Add 201 Sell 105/40\n";
    Order o3{ 201, Side::Sell, 105, 40 };
    AddOrder(&book, o3);
    PrintBook(book);

    std::cout << "Step 4: Cancel 101\n";
    bool cancelled = CancelOrder(&book, 101);
    std::cout << "  cancel result=" << cancelled << "\n";
    PrintBook(book);

    std::cout << "Step 5: Modify 102 -> 103, price 99, qty 25\n";
    OrderModify mod{ 102, 103, Side::Buy, 99, 25 };
    ModifyOrder(&book, mod);
    PrintBook(book);

    std::cout << "Step 6: Cancel 999 (nonexistent)\n";
    bool cancelled2 = CancelOrder(&book, 999);
    std::cout << "  cancel result=" << cancelled2 << "\n";
    PrintBook(book);

    return 0;
}