/*
   golden_api.h
   ----------------
   C interface to the golden model for testbenches: DPI-C from the UVM
   testbench (hardware-orderbook/tb/uvm) or ctypes from cocotb. Plain
   integers only, so neither side needs svdpi.h or C++ types, and any
   compiler can build it.

   Each call to golden_apply is one book command, driven exactly as the RTL
   book core is driven (see command.h). After it, the scoreboard can:
   - compare the BBO (golden_bbo) against the RTL's snapshot packet,
   - compare every memory write the command caused (golden_write_*)
     against the RTL's memory write ports, in order,
   - read any table / CAM entry or level (golden_*_get) for a full-state
     compare at end of test.

   The model is a single global book: one testbench, one book.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Empties the book and sets the session base price (must be on the tick
   grid -- golden_tick() -- like the RTL's base-price register). */
void golden_reset(unsigned int base_price);

/* Applies one command. typ is the ITCH type char: 'A' 'F' 'E' 'C' 'X' 'D'
   'U'. side: 0 = buy, 1 = sell (adds only). For E/C/X, shares is the
   executed/cancelled amount; for 'U', oref is the old ORN and new_ref,
   shares, price the replacement.
   Returns the snapshot status bits the command raises (bit 0
   order_dropped, 1 price_rejected, 3 unknown_order_ref), or -1 for an
   unsupported typ. */
int golden_apply(unsigned char typ, unsigned char side,
                 unsigned long long oref, unsigned long long new_ref,
                 unsigned int shares, unsigned int price);

/* Best bid and offer after the last command; an empty side reads 0s. */
void golden_bbo(unsigned int* bid_price, unsigned int* bid_shares, unsigned int* bid_orders,
                unsigned int* ask_price, unsigned int* ask_shares, unsigned int* ask_orders);

/* Memory writes the last command caused, in order (0..4 of them).
   target: 0 table (index = set*ways + way), 1 CAM (index = entry),
           2 bid level, 3 ask level (index = slot).
   Table/CAM writes fill valid, oref, side, slot, shares; level writes fill
   shares and orders. Unused outputs are written 0. */
int  golden_write_count(void);
void golden_write_get(int i, unsigned int* target, unsigned int* index,
                      unsigned int* valid, unsigned long long* oref, unsigned int* side,
                      unsigned int* slot, unsigned int* shares, unsigned int* orders);

/* Direct reads of the book's memories. addr for the table is
   set*ways + way. */
void golden_table_get(unsigned int addr, unsigned int* valid, unsigned long long* oref,
                      unsigned int* side, unsigned int* slot, unsigned int* shares);
void golden_cam_get(unsigned int entry, unsigned int* valid, unsigned long long* oref,
                    unsigned int* side, unsigned int* slot, unsigned int* shares);
void golden_level_get(unsigned int side, unsigned int slot,
                      unsigned int* shares, unsigned int* orders);

/* Geometry, so a testbench can size its loops from the model rather than
   restating constants. */
unsigned int golden_sets(void);
unsigned int golden_ways(void);
unsigned int golden_cam_depth(void);
unsigned int golden_slots(void);
unsigned int golden_tick(void);

#ifdef __cplusplus
}
#endif
