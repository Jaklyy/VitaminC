#include <string.h>
#include <stdio.h>
#include "interpreter.h"
#include "../../patternmatch.h"
#include "../../types.h"
#include "../../bus.h"

// stolen from melonds oops
alignas(32) static const u16 CondLookup[16] =
{
    0xF0F0, // EQ
    0x0F0F, // NE
    0xCCCC, // CS
    0x3333, // CC
    0xFF00, // MI
    0x00FF, // PL
    0xAAAA, // VS
    0x5555, // VC
    0x0C0C, // HI
    0xF3F3, // LS
    0xAA55, // GE
    0x55AA, // LT
    0x0A05, // GT
    0xF5FA, // LE
    0xFFFF, // AL
    0x0000  // NV
};

inline u32 ARM11_ROR32(u32 val, u8 ror)
{
	return (val >> ror) | (val << (32-ror));
}

#define CHECK(x, y, z) if (PatternMatch((struct Pattern) {0b##x, 0b##y}, bits)) { return z; } else

void* ARM11_InitARMInstrLUT(const u16 bits)
{
	// media instruction space
	CHECK(011000000001, 111110000001, NULL) // parallel add/sub
	CHECK(011010000001, 111111110011, NULL) // halfword pack
	CHECK(011010100001, 111110100011, NULL) // word saturate
	CHECK(011010100011, 111110111111, NULL) // parallel halfword saturate
	CHECK(011010110011, 111111111111, NULL) // byte reverse word
	CHECK(011010111011, 111111111111, NULL) // byte reverse packed halfword
	CHECK(011011111011, 111111111111, NULL) // byte reverse signed halfword
	CHECK(011010001011, 111111111111, NULL) // select bytes
	CHECK(011010000111, 111110001111, NULL) // sign/zero extend (add)
	CHECK(011100000001, 111110000001, NULL) // multiplies (type 3)
	CHECK(011110000001, 111111111111, NULL) // unsigned sum of absolute differences (also acc variant?)
	// multiply extension space
	CHECK(000000001001, 111111001111, NULL) // short multiplies
	CHECK(000001001001, 111111111111, NULL) // umaal
	CHECK(000010001001, 111110001111, NULL) // long multiplies
	// control/dsp extension space
	CHECK(000100000000, 111110111111, ARM11_MRS) // mrs
	CHECK(000100100000, 111110111111, ARM11_MSRReg) // msr (reg)
	CHECK(001100100000, 111110110000, ARM11_MSRImm_Hints) // msr (imm) / hints
	CHECK(000100100001, 111111111101, ARM_BX_BLXReg) // bx/blx (reg)
	CHECK(000100100010, 111111111111, NULL) // bxj
	CHECK(000101100001, 111111111111, NULL) // clz
	CHECK(000100000100, 111110011111, NULL) // q(d)add/sub
	CHECK(000100100111, 111111111111, NULL) // bkpt
	CHECK(000100001000, 111110011001, NULL) // signed multiplies
	// load/store extension space
	CHECK(000100001001, 111110111111, NULL) // swp
	CHECK(000110001001, 111110001111, NULL) // ldrex/strex (and variants)
	CHECK(000000001011, 111000001111, NULL) // ldrh/strh
	CHECK(000000011101, 111000011101, NULL) // ldrsh/ldrsb
	CHECK(000000001101, 111000011101, NULL) // ldrd/strd
	// explicitly defined undefined space
	CHECK(011111111111, 111111111111, NULL) // undef instr
	// coproc extension space
	CHECK(110000000000, 111110100000, NULL) // coprocessor?
	// data processing
	CHECK(000000000000, 110000000000, ARM11_ALU) // alu
	// load/store
	CHECK(010000000000, 110000000000, ARM11_LDR_STR) // load/store
	// coprocessor data processing
	CHECK(111000000000, 111100000001, NULL) // cdp?
	// coprocessor register transfers
	CHECK(111000000001, 111100010001, ARM11_MCR_MRC) // mcr
	CHECK(111000010001, 111100010001, ARM11_MCR_MRC) // mrc
	// multiple load/store
	CHECK(100000000000, 111000000000, ARM11_LDM_STM) // ldm/stm
	// branch
	CHECK(101000000000, 111000000000, ARM11_B_BL) // b/bl

	return NULL; // undef instr (raise exception)
}

void* ARM11_InitTHUMBInstrLUT(const u8 bits)
{
	CHECK(000110, 111110, THUMB11_ADD_SUB_Reg_Imm3); // add/sub reg/imm
	CHECK(000000, 111000, THUMB11_ShiftImm); // shift by imm
	CHECK(001000, 111000, THUMB11_ADD_SUB_CMP_MOV_Imm8); // add/sub/cmp/mov imm
	CHECK(010000, 111111, THUMB11_ALU); // data proc reg
	CHECK(010001, 111111, NULL); // spec data proc/b(l)x
	CHECK(010010, 111110, NULL); // ldr pcrel
	CHECK(010100, 111100, NULL); // ldr/str reg
	CHECK(011000, 111000, NULL); // (ld/st)r(b) imm
	CHECK(100000, 111100, NULL); // ldrh/strh imm
	CHECK(100100, 111100, NULL); // ldr/str sprel
	CHECK(101100, 111100, NULL); // misc
	CHECK(110000, 111100, NULL); // ldm/stm
	CHECK(110100, 111100, NULL); // cond b/udf/svc
	CHECK(111000, 111110, NULL); // b
	CHECK(111100, 111110, NULL); // blx prefix
	CHECK(111010, 111010, NULL); // bl suffix
	return NULL; // udf
}

void* DecodeUncondInstr(const u32 bits)
{
	CHECK(0001000000000000000000000000, 1111111100010000000000100000, NULL) // cps
	CHECK(0001000000010000000000000000, 1111111111110000000011110000, NULL) // setend
	CHECK(0101010100001111000000000000, 1101011100001111000000000000, NULL) // pld
	CHECK(1000010011010000010100000000, 1110010111110000111100000000, NULL) // srs
	CHECK(1000000100000000101000000000, 1110010100000000111100000000, NULL) // rfe
	CHECK(1010000000000000000000000000, 1110000000000000000000000000, NULL) // blx (imm)
	CHECK(1100010000000000000000000000, 1111111000000000000000000000, NULL) // cp double reg
	CHECK(1110000000000000000000010000, 1111000000000000000000010000, NULL) // cp reg transfer?
	CHECK(1111000000000000000000000000, 1111000000000000000000000000, NULL) // undef instr
	return NULL; // undefined instr? (checkme)
}

#undef CHECK

alignas(64) void (*ARM11_InstrLUT[0x1000]) (struct ARM11MPCore* ARM11);
alignas(64) void (*THUMB11_InstrLUT[0x40]) (struct ARM11MPCore* ARM11);

struct ARM11MPCore ARM11[4];

char* ARM11_Init()
{
	memset(ARM11, 0, sizeof(ARM11));
	for (int i = 0; i < 4; i++)
	{
		ARM11[i].NextStep = ARM11_StartFetch;
		ARM11_Branch(&ARM11[i], 0, false);
		ARM11[i].Mode = MODE_SVC;
		ARM11->SPSR = &ARM11->SVC.SPSR;
		ARM11[i].CP15.Control = 0x00054078;
		ARM11[i].CP15.AuxControl = 0x0F;
		ARM11[i].CP15.DCacheLockdown = 0xFFFFFFF0;
		ARM11[i].CPUID = i;
	}
	return NULL;
}

void ARM11_UpdateMode(struct ARM11MPCore* ARM11, u8 newmode)
{
	printf("%X, %X\n", ARM11->Mode, newmode);

	if (ARM11->Mode == newmode) return;

	switch (ARM11->Mode)
	{
	case 0x10: // USR
	case 0x1F: // SYS
		memcpy(&ARM11->USR.R8, &ARM11->R8, 7*4);
		break;

	case 0x11: // FIQ
		memcpy(&ARM11->FIQ.R8, &ARM11->R8, 7*4);
		break;

	case 0x12: // IRQ
		memcpy(&ARM11->USR.R8, &ARM11->R8, 5*4);
		memcpy(&ARM11->IRQ.SP, &ARM11->SP, 2*4);
		break;

	case 0x13: // SVC
		memcpy(&ARM11->USR.R8, &ARM11->R8, 5*4);
		memcpy(&ARM11->SVC.SP, &ARM11->SP, 2*4);
		break;

	case 0x17: // ABT
		memcpy(&ARM11->USR.R8, &ARM11->R8, 5*4);
		memcpy(&ARM11->ABT.SP, &ARM11->SP, 2*4);
		break;
	
	case 0x1B: // UND
		memcpy(&ARM11->USR.R8, &ARM11->R8, 5*4);
		memcpy(&ARM11->UND.SP, &ARM11->SP, 2*4);
		break;
	}

	switch (newmode)
	{
	case 0x10: // USR
	case 0x1F: // SYS
		memcpy(&ARM11->R8, &ARM11->USR.R8, 7*4);
		ARM11->SPSR = NULL;
		break;

	case 0x11: // FIQ
		memcpy(&ARM11->R8, &ARM11->FIQ.R8, 7*4);
		ARM11->SPSR = &ARM11->FIQ.SPSR;
		break;

	case 0x12: // IRQ
		memcpy(&ARM11->R8, &ARM11->USR.R8, 5*4);
		memcpy(&ARM11->SP, &ARM11->IRQ.SP, 2*4);
		ARM11->SPSR = &ARM11->IRQ.SPSR;
		break;

	case 0x13: // SVC
		memcpy(&ARM11->R8, &ARM11->USR.R8, 5*4);
		memcpy(&ARM11->SP, &ARM11->SVC.SP, 2*4);
		ARM11->SPSR = &ARM11->SVC.SPSR;
		break;

	case 0x17: // ABT
		memcpy(&ARM11->R8, &ARM11->USR.R8, 5*4);
		memcpy(&ARM11->SP, &ARM11->ABT.SP, 2*4);
		ARM11->SPSR = &ARM11->ABT.SPSR;
		break;
	
	case 0x1B: // UND
		memcpy(&ARM11->R8, &ARM11->USR.R8, 5*4);
		memcpy(&ARM11->SP, &ARM11->UND.SP, 2*4);
		ARM11->SPSR = &ARM11->UND.SPSR;
		break;
	}
}

void ARM11_MRS(struct ARM11MPCore* ARM11)
{
	const u32 curinstr = ARM11->Instr.Data;
	const bool r = curinstr & (1<<22);
	const u8 rd = (curinstr >> 12) & 0xF;

	u32 psr;
	if (r) psr = ARM11->SPSR ? *ARM11->SPSR : 0;
	else psr = ARM11->CPSR;
	ARM11_WriteReg(ARM11, rd, psr, false);
}

void ARM11_MSR(struct ARM11MPCore* ARM11, u32 val)
{
	const u32 curinstr = ARM11->Instr.Data;
	const bool r = curinstr & (1<<22);
	const bool c = curinstr & (1<<16);
	const bool x = curinstr & (1<<17);
	const bool s = curinstr & (1<<18);
	const bool f = curinstr & (1<<19);

	u32 writemask;
	if (ARM11->Mode == MODE_USR)
	{
		writemask = 0xF80F0200;
	}
	else
	{
		writemask = 0xF90F03FF;
	}

	if (!c) writemask &= 0xFFFFFF00;
	if (!x) writemask &= 0xFFFF00FF;
	if (!s) writemask &= 0xFF00FFFF;
	if (!f) writemask &= 0x00FFFFFF;

	printf("%08X\n", writemask);
	val &= writemask;

	if (r)
	{
		if (!ARM11->SPSR) return;
		*ARM11->SPSR &= ~writemask;
		*ARM11->SPSR |= val;
	}
	else
	{
		if (val & 0x01000020) printf("SETTING T OR J BIT!!! PANIC!!!!!!\n");

		val |= ARM11->CPSR & ~writemask;
		ARM11_UpdateMode(ARM11, val&0x1F);
		ARM11->CPSR = val;
	}
}

void ARM11_MSRReg(struct ARM11MPCore* ARM11)
{
	const u32 curinstr = ARM11->Instr.Data;
	const u32 val = ARM11_GetReg(ARM11, curinstr & 0xF);

	ARM11_MSR(ARM11, val);
}

void ARM11_MSRImm_Hints(struct ARM11MPCore* ARM11)
{
	const u32 curinstr = ARM11->Instr.Data;
	const bool op = curinstr & (1<<22);
	const u8 op1 = (curinstr >> 16) & 0xF;
	const u8 op2 = curinstr & 0xFF;

	if (op1) // msr immediate
	{
		const u8 rorimm = ((curinstr >> 8) & 0xF) * 2;
		const u32 val = ARM11_ROR32(op2, rorimm);
		ARM11_MSR(ARM11, val);
	}
	else if (!op) // hints
	{
		switch(op2)
		{
		case 0x00: // nop
			return;
		case 0x01: // yield
			printf("UNIMPLEMENTED HINT: YIELD!!!\n"); return;
		case 0x02: // wfe (wait for event)
			printf("UNIMPLEMENTED HINT: WFE\n"); return;
		case 0x03: // wfi (wait for interrupt)
			printf("UNIMPLEMENTED HINT: WFI\n"); return;
		case 0x04: // sev (send event)
			printf("UNIMPLEMENTED HINT: SEV\n"); return;
		case 0x14: // csdb (consumption of speculative data barrier?????????)
			printf("UNIMPLEMENTED HINT: CSDB\n"); return;
		default:
			printf("INVALID HINT %02X\n", op2); return;
		}
	}
}

void ARM11_Branch(struct ARM11MPCore* ARM11, const u32 addr, const bool restore)
{
	printf("Jumping to %08X\n", addr);
	ARM11->PC = addr;

	if (restore)
	{
		const u32 spsr = *ARM11->SPSR;
		ARM11_UpdateMode(ARM11, spsr & 0x1F);
		ARM11->CPSR = spsr;
	}
}

inline u32 ARM11_GetReg(struct ARM11MPCore* ARM11, const int reg)
{
	if (reg == 15)
	{
		return ARM11->PC + 4;
	}
	else return ARM11->R[reg];
}

inline void ARM11_WriteReg(struct ARM11MPCore* ARM11, const int reg, const u32 val, const bool restore)
{
	if (reg == 15)
	{
		ARM11_Branch(ARM11, val, restore);
	}
	else
	{
		ARM11->R[reg] = val;
	}
}

u32 ARM11_CodeFetch(struct ARM11MPCore* ARM11)
{
	return Bus11_InstrLoad32(ARM11, ARM11->PC);
}

void ARM11_StartFetch(struct ARM11MPCore* ARM11)
{
	ARM11->Instr.Data = ARM11_CodeFetch(ARM11); 
	ARM11->PC += 4;
}

void ARM11_StartExec(struct ARM11MPCore* ARM11)
{
    const u32 instr = ARM11->Instr.Data;
    const u8 condcode = instr >> 28;
	
	// Todo: handle IRQs
	if (CondLookup[condcode] & (1<<ARM11->Flags))
	{
    	const u16 decodebits = ((instr >> 16) & 0xFF0) | ((instr >> 4) & 0xF);

		if (ARM11_InstrLUT[decodebits])
			(ARM11_InstrLUT[decodebits])(ARM11);
		else
		{
			printf("UNIMPL INSTR: %08X %08X!!!\n", ARM11->Instr.Data, ARM11->PC);
			for (int i = 0; i < 16; i++) printf("%i, %08X ", i, ARM11->R[i]);
			while (true)
				;
		}
		for (int i = 0; i < 16; i++) printf("%i, %08X ", i, ARM11->R[i]);
		printf("\n");
	}
	else if (condcode == COND_NV) // do special handling for unconditional instructions
	{
		printf("UNCOND: %08X!!!!\n", ARM11->Instr.Data);
		//DecodeUncondInstr(instr);
	}
	else if (false) // TODO: handle illegal BKPTs? might still execute regardless of condition fail like the ARM9?
	{
	}
	else
	{
		// instruction was not executed.
		printf("INSTR: %08X SKIP\n", ARM11->Instr.Data);
	}
}

void ARM11_RunInterpreter(struct ARM11MPCore* ARM11, u64 target)
{
	while (ARM11->Timestamp <= target)
	{
        //(ARM11->NextStep)(ARM11);
		ARM11_StartFetch(ARM11);
		ARM11_StartExec(ARM11);
		ARM11->Timestamp++;
		printf("times: %li %li\n", ARM11->Timestamp, target);
	}
}
