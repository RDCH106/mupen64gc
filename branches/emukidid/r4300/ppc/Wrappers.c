/* Wrappers.c - Dynarec trampoline and other functions to simplify access from dynarec
   by Mike Slegeir for Mupen64-GC
 */

#include <stdlib.h>
#include "../../gui/DEBUG.h"
#include "../Invalid_Code.h"
#include "../../gc_memory/memory.h"
#include "../interupt.h"
#include "../r4300.h"
#include "Wrappers.h"

extern int stop;
extern unsigned long instructionCount;
extern void (*interp_ops[64])(void);
inline unsigned long update_invalid_addr(unsigned long addr);

int noCheckInterrupt = 0;

/* Recompiled code stack frame:
 *  $sp+12  |
 *  $sp+8   | old cr
 *  $sp+4   | old lr
 *  $sp	    | old sp
 */

inline unsigned int dyna_run(unsigned int (*code)(void)){
	__asm__ volatile(
		"stwu	1, -16(1) \n"
		"mfcr	14        \n"
		"stw	14, 8(1)  \n"
		"mr	14, %0    \n"
		"addi	15, 0, 0  \n"
		"mr	16, %1    \n"
		"mr	17, %2    \n"
		"mr	18, %3    \n"
		"mr	19, %4    \n"
		"mr	20, %5    \n"
		"mr	21, %6    \n"
		"mr	22, %7    \n"
		:: "r" (reg), "r" (decodeNInterpret),
		   "r" (dyna_update_count), "r" (&last_addr),
		   "r" (rdram), "r" (SP_DMEM),
		   "r" (reg_cop1_simple), "r" (reg_cop1_double)
		: "14", "15", "16", "17", "18", "19", "20", "21", "22");
	
	unsigned int naddr = code();
	
	__asm__ volatile("lwz	1, 0(1)\n");
	
	return naddr;
}

void dynarec(unsigned int address){
	while(!stop){
		PowerPC_block* dst_block = blocks[address>>12];
		unsigned long paddr = update_invalid_addr(address);
		/*
		sprintf(txtbuffer, "trampolining to 0x%08x\n", address);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		*/
		if(!paddr){ stop=1; return; }
		
		if(!dst_block){
			/*sprintf(txtbuffer, "block at %08x doesn't exist\n", address&~0xFFF);
			DEBUG_print(txtbuffer, DBG_USBGECKO);*/
			blocks[address>>12] = malloc(sizeof(PowerPC_block));
			dst_block = blocks[address>>12];
			dst_block->code          = NULL;
			dst_block->code_addr     = NULL;
			dst_block->start_address = address & ~0xFFF;
			dst_block->end_address   = (address & ~0xFFF) + 0x1000;
			init_block(rdram+(((paddr-(address-dst_block->start_address)) & 0x1FFFFFFF)>>2),
				   dst_block);
		}
		
		if(invalid_code_get(address>>12)){
			/*sprintf(txtbuffer, "block at %08x is invalid\n", address&~0xFFF);
			DEBUG_print(txtbuffer, DBG_USBGECKO);*/
			dst_block->length = 0;
			start_section(COMPILER_SECTION);
			recompile_block(dst_block);
			end_section(COMPILER_SECTION);
		} else {
			/*static last_block = -1;
			if(address>>12 != last_block)
				RecompCache_Update(address>>12);
			last_block = address>>12;*/
		}
		
		// Recompute the block offset
		unsigned int (*code)(void);
		code = (unsigned int (*)(void))dst_block->code_addr[(address&0xFFF)>>2];
		/*
		sprintf(txtbuffer, "Entering dynarec code @ 0x%08x\n", code);
		DEBUG_print(txtbuffer, DBG_USBGECKO);
		*/
		address = dyna_run(code);
		
		if(!noCheckInterrupt){
			last_addr = interp_addr = address;
			// Check for interrupts
			if(next_interupt <= Count){
				gen_interupt();
				address = interp_addr;
			}
		}
		noCheckInterrupt = 0;
	}
}

unsigned int decodeNInterpret(MIPS_instr mips, unsigned int pc,
                              int isDelaySlot){
	delay_slot = isDelaySlot; // Make sure we set delay_slot properly
	PC->addr = interp_addr = pc;
	prefetch_opcode(mips);
	interp_ops[MIPS_GET_OPCODE(mips)]();
	delay_slot = 0;
	
	if(interp_addr != pc + 4) noCheckInterrupt = 1;
	
	return interp_addr != pc + 4 ? interp_addr : 0;
}

int dyna_update_count(unsigned int pc){
	Count += (pc - last_addr)/2;
	last_addr = pc;
	
	return next_interupt - Count;
}

