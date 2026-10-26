/*
   ordertable.cpp
   ----------------
   Order table lookup. See include/ordertable.h for the rules this shares
   with the RTL.

*/

#include "../include/ordertable.h"

TableLoc TableFind(const OrderTable& table, const OrderId orn) {
	const uint32_t set = HashOrn(orn);
	for (uint32_t w = 0; w < WAYS; w++) {
		const TableEntry& e = table.ways[set][w];
		if (e.valid && e.orderId == orn) return {true, false, set, w};
	}
	for (uint32_t i = 0; i < CAM_DEPTH; i++) {
		const TableEntry& e = table.cam[i];
		if (e.valid && e.orderId == orn) return {true, true, set, i};
	}
	return {false, false, set, 0};
}

TableLoc TableFindFree(const OrderTable& table, const OrderId orn) {
	const uint32_t set = HashOrn(orn);
	for (uint32_t w = 0; w < WAYS; w++)
		if (!table.ways[set][w].valid) return {true, false, set, w};
	for (uint32_t i = 0; i < CAM_DEPTH; i++)
		if (!table.cam[i].valid) return {true, true, set, i};
	return {false, false, set, 0};
}

TableEntry& TableAt(OrderTable& table, const TableLoc& loc) {
	return loc.inCam ? table.cam[loc.index] : table.ways[loc.set][loc.index];
}

const TableEntry& TableAt(const OrderTable& table, const TableLoc& loc) {
	return loc.inCam ? table.cam[loc.index] : table.ways[loc.set][loc.index];
}

uint32_t TableAddress(const TableLoc& loc) {
	return loc.inCam ? loc.index : loc.set * WAYS + loc.index;
}
