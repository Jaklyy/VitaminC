#include <string.h>
#include <stdio.h>
#include "arm.h"
#include "../../utils.h"
#include "bus.h"


inline u32 ARM9_ROR32(u32 val, u8 ror)
{
	return (val >> ror) | (val << (32-ror));
}

#define CHECK(x, y, z) if (PatternMatch((struct Pattern) {0b##x, 0b##y}, bits)) { z(ARM9); } else

void THUMB9_DecodeMiscThumb(struct ARM946E_S* ARM9)
{
	const u16 bits = (ARM9->Instr[0].Data >> 3) & 0x1FF;

	CHECK(000000000, 111100000, THUMB9_ADD_SUB_SP) // adjust stack ptr
	CHECK(010000000, 111000000, THUMB9_PUSH) // push
	CHECK(110000000, 111000000, THUMB9_POP) // pop
	//CHECK(111000000, 111000000, NULL) // bkpt
	//NULL();
	printf("UNIMPLEMENTED MISC THUMB INSTRUCTION %04X\n", ARM9->Instr[0].Data);
}

#undef CHECK
#define CHECK(x, y, z) if (PatternMatch((struct Pattern) {0b##x, 0b##y}, bits)) { return z; } else

void* ARM9_InitARMInstrLUT(const u16 bits)
{
	// multiply extension space
	CHECK(000000001001, 111111001111, ARM9_MUL_MLA) // short multiplies
	CHECK(000010001001, 111110001111, NULL) // long multiplies
	// control/dsp extension space
	CHECK(000100000000, 111110111111, ARM9_MRS) // mrs
	CHECK(000100100000, 111110111111, ARM9_MSRReg) // msr (reg)
	CHECK(001100100000, 111110110000, ARM9_MSRImm) // msr (imm)
	CHECK(000100100001, 111111111101, ARM9_BX_BLXReg) // bx/blx (reg)
	CHECK(000100100010, 111111111111, NULL) // bxj checkme: does this exist on arm9? does it decode to anything of use?
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
	CHECK(110000000000, 111110100000, NULL) // coprocessor? - checkme: longer undef?
	// data processing
	CHECK(000000000000, 110000000000, ARM9_ALU) // alu
	// load/store
	CHECK(010000000000, 110000000000, ARM9_LDR_STR) // load/store
	// coprocessor data processing
	CHECK(111000000000, 111100000001, NULL) // cdp? - checkme: longer undef?
	// coprocessor register transfers
	CHECK(111000000001, 111100010001, ARM9_MCR_MRC) // mcr
	CHECK(111000010001, 111100010001, ARM9_MCR_MRC) // mrc
	// multiple load/store
	CHECK(100000000000, 111000000000, ARM9_LDM_STM) // ldm/stm
	// branch
	CHECK(101000000000, 111000000000, ARM9_B_BL) // b/bl

	return NULL; // undef instr (raise exception)
}

void* ARM9_InitTHUMBInstrLUT(const u8 bits)
{
	CHECK(000110, 111110, THUMB9_ADD_SUB_Reg_Imm3); // add/sub reg/imm
	CHECK(000000, 111000, THUMB9_ShiftImm); // shift by imm
	CHECK(001000, 111000, THUMB9_ADD_SUB_CMP_MOV_Imm8); // add/sub/cmp/mov imm
	CHECK(010000, 111111, THUMB9_ALU); // data proc reg
	CHECK(010001, 111111, THUMB9_ALU_HI); // spec data proc/b(l)x
	CHECK(010010, 111110, THUMB9_LDRPCRel); // ldr pcrel
	CHECK(010100, 111100, THUMB9_LDR_STR_Reg); // ldr/str reg
	CHECK(011000, 111000, THUMB9_LDR_STR_Imm5); // (ld/st)r(b) imm
	CHECK(100000, 111100, THUMB9_LDRH_STRH_Imm5); // ldrh/strh imm
	CHECK(100100, 111100, THUMB9_LDR_STR_SPRel); // ldr/str sprel
	CHECK(101000, 111100, THUMB9_ADD_SP_PCRel); // add sp/pcrel
	CHECK(101100, 111100, THUMB9_DecodeMiscThumb); // misc
	CHECK(110000, 111100, THUMB9_LDMIA_STMIA); // ldm/stm
	CHECK(110100, 111100, THUMB9_CondB_SWI); // cond b/udf/svc
	CHECK(111000, 111110, THUMB9_B); // b
	CHECK(111100, 111110, THUMB9_BL_BLX_LO); // bl(x) prefix
	CHECK(111010, 111010, THUMB9_BL_BLX_HI); // bl(x) suffix
	return NULL; // udf
}

void* ARM9_DecodeUncondInstr(const u32 bits)
{
	CHECK(0101010100001111000000000000, 1101011100001111000000000000, ARM9_PLD) // pld
	CHECK(1010000000000000000000000000, 1110000000000000000000000000, ARM9_BLX_Imm) // blx (imm)
	CHECK(1100010000000000000000000000, 1111111000000000000000000000, NULL) // cp double reg - longer undef?
	CHECK(1110000000000000000000010000, 1111000000000000000000010000, NULL) // cp reg transfer? - longer undef?
	CHECK(1111000000000000000000000000, 1111000000000000000000000000, NULL) // undef instr
	return NULL; // undefined instr? (checkme)
}

#undef CHECK

alignas(64) void (*ARM9_InstrLUT[0x1000]) (struct ARM946E_S* ARM9);
alignas(64) void (*THUMB9_InstrLUT[0x40]) (struct ARM946E_S* ARM9);

struct ARM946E_S _ARM9;

char* ARM9_Init()
{
	memset(&_ARM9, 0, sizeof(_ARM9));
	_ARM9.NextStep = ARM9_StartFetch;
	ARM9_Branch(&_ARM9, 0xFFFF0000, false);
	_ARM9.Mode = MODE_SVC;
	_ARM9.SPSR = &_ARM9.SVC.SPSR;
	_ARM9.CP15.Control.Data = 0x00002078; // checkme: itcm could be enabled on boot?
	_ARM9.CP15.DTCMRegion.Size = 5;
	_ARM9.CP15.ITCMRegion.Size = 6;
	_ARM9.ITCMMask = 0xFFFFFFFF;
    _ARM9.DTCMMask = 0x00000000;
    _ARM9.DTCMBase = 0xFFFFFFFF;
	return NULL;
}

void ARM9_UpdateMode(struct ARM946E_S* ARM9, u8 oldmode, u8 newmode)
{
	if (oldmode == newmode) return;

	switch (oldmode)
	{
	case 0x10: // USR
	case 0x1F: // SYS
		memcpy(&ARM9->USR.R8, &ARM9->R8, 7*4);
		break;

	case 0x11: // FIQ
		memcpy(&ARM9->FIQ.R8, &ARM9->R8, 7*4);
		break;

	case 0x12: // IRQ
		memcpy(&ARM9->USR.R8, &ARM9->R8, 5*4);
		memcpy(&ARM9->IRQ.SP, &ARM9->SP, 2*4);
		break;

	case 0x13: // SVC
		memcpy(&ARM9->USR.R8, &ARM9->R8, 5*4);
		memcpy(&ARM9->SVC.SP, &ARM9->SP, 2*4);
		break;

	case 0x17: // ABT
		memcpy(&ARM9->USR.R8, &ARM9->R8, 5*4);
		memcpy(&ARM9->ABT.SP, &ARM9->SP, 2*4);
		break;
	
	case 0x1B: // UND
		memcpy(&ARM9->USR.R8, &ARM9->R8, 5*4);
		memcpy(&ARM9->UND.SP, &ARM9->SP, 2*4);
		break;
	}

	switch (newmode)
	{
	case 0x10: // USR
	case 0x1F: // SYS
		memcpy(&ARM9->R8, &ARM9->USR.R8, 7*4);
		ARM9->SPSR = NULL;
		break;

	case 0x11: // FIQ
		memcpy(&ARM9->R8, &ARM9->FIQ.R8, 7*4);
		ARM9->SPSR = &ARM9->FIQ.SPSR;
		break;

	case 0x12: // IRQ
		memcpy(&ARM9->R8, &ARM9->USR.R8, 5*4);
		memcpy(&ARM9->SP, &ARM9->IRQ.SP, 2*4);
		ARM9->SPSR = &ARM9->IRQ.SPSR;
		break;

	case 0x13: // SVC
		memcpy(&ARM9->R8, &ARM9->USR.R8, 5*4);
		memcpy(&ARM9->SP, &ARM9->SVC.SP, 2*4);
		ARM9->SPSR = &ARM9->SVC.SPSR;
		break;

	case 0x17: // ABT
		memcpy(&ARM9->R8, &ARM9->USR.R8, 5*4);
		memcpy(&ARM9->SP, &ARM9->ABT.SP, 2*4);
		ARM9->SPSR = &ARM9->ABT.SPSR;
		break;
	
	case 0x1B: // UND
		memcpy(&ARM9->R8, &ARM9->USR.R8, 5*4);
		memcpy(&ARM9->SP, &ARM9->UND.SP, 2*4);
		ARM9->SPSR = &ARM9->UND.SPSR;
		break;
	}
}

void ARM9_MRS(struct ARM946E_S* ARM9)
{
	const u32 curinstr = ARM9->Instr[0].Data;
	const bool r = curinstr & (1<<22);
	const u8 rd = (curinstr >> 12) & 0xF;

	u32 psr;
	if (r) psr = ARM9->SPSR ? *ARM9->SPSR : 0;
	else psr = ARM9->CPSR;
	ARM9_WriteReg(ARM9, rd, psr, false, false);
}

void ARM9_MSR(struct ARM946E_S* ARM9, u32 val)
{
	const u32 curinstr = ARM9->Instr[0].Data;
	const bool r = curinstr & (1<<22);
	const bool c = curinstr & (1<<16);
	const bool x = curinstr & (1<<17);
	const bool s = curinstr & (1<<18);
	const bool f = curinstr & (1<<19);

	u32 writemask;
	if (ARM9->Mode == MODE_USR)
	{
		writemask = 0xF8000000;
	}
	else
	{
		writemask = 0xF80000EF;
		if (!c) writemask &= 0xFFFFFF00;
	}

	if (!f) writemask &= 0x00FFFFFF;

	val &= writemask;

	if (r)
	{
		if (!ARM9->SPSR) return;
		*ARM9->SPSR &= ~writemask;
		*ARM9->SPSR |= val;
	}
	else
	{
		if (val & 0x01000020) printf("MSR SETTING T BIT!!! PANIC!!!!!!\n");

		val |= ARM9->CPSR & ~writemask;
		ARM9_UpdateMode(ARM9, ARM9->Mode, val&0x1F);
		ARM9->CPSR = val;
		printf("CPSR UPDATE: %08X\n", ARM9->CPSR);
	}
}

void ARM9_MSRReg(struct ARM946E_S* ARM9)
{
	const u32 curinstr = ARM9->Instr[0].Data;
	const u32 val = ARM9_GetReg(ARM9, curinstr & 0xF);

	ARM9_MSR(ARM9, val);
}

void ARM9_MSRImm(struct ARM946E_S* ARM9)
{
	const u32 curinstr = ARM9->Instr[0].Data;
	const bool op = curinstr & (1<<22);
	const u8 op1 = (curinstr >> 16) & 0xF;
	const u8 op2 = curinstr & 0xFF;

	const u8 rorimm = ((curinstr >> 8) & 0xF) * 2;
	const u32 val = ARM9_ROR32(op2, rorimm);
	ARM9_MSR(ARM9, val);
}

u32 ARM9_CodeFetch(struct ARM946E_S* ARM9)
{
	return Bus9_InstrLoad32(ARM9, ARM9->PC);
}

void ARM9_Branch(struct ARM946E_S* ARM9, u32 addr, const bool restore)
{
#if 1
	if (addr != 0xFFFF8208)
		printf("ARM9: Jumping to %08X from %08X via %08X\n", addr, ARM9->PC, ARM9->Instr[0].Data);
#endif
	if (restore)
	{
		const u32 spsr = *ARM9->SPSR;
		ARM9_UpdateMode(ARM9, ARM9->Mode, spsr & 0x1F);
		ARM9->CPSR = spsr;
		printf("ARM9 - CPSR RESTORE: %08X\n", ARM9->CPSR);

		addr &= ~0x1;
		addr |= ARM9->Thumb;
	}

	if (addr & 0x1)
	{
		ARM9->Thumb = true;

		addr &= ~0x1;
		ARM9->PC = addr;

		if (addr & 0x2)
		{
			ARM9->Instr[0].Data = ARM9_CodeFetch(ARM9);
		}
	}
	else
	{
		ARM9->Thumb = false;

		addr &= ~0x3;
		ARM9->PC = addr;
	}
}

inline u32 ARM9_GetReg(struct ARM946E_S* ARM9, const int reg)
{
	if (reg == 15)
	{
		return ARM9->PC + (ARM9->Thumb ? 2 : 4);
	}
	else return ARM9->R[reg];
}

inline void ARM9_WriteReg(struct ARM946E_S* ARM9, const int reg, u32 val, const bool restore, const bool canswap)
{
	if (reg == 15)
	{
		if (!canswap)
		{
			if (ARM9->Thumb) val |= 1;
			else val &= ~1;
		}
		ARM9_Branch(ARM9, val, restore);
	}
	else
	{
		ARM9->R[reg] = val;
	}
}

void ARM9_StartFetch(struct ARM946E_S* ARM9)
{
	if (!(ARM9->PC & 0x2)) ARM9->Instr[0].Data = ARM9_CodeFetch(ARM9);
	else ARM9->Instr[0].Data >>= 16;

	ARM9->PC += (ARM9->Thumb ? 2 : 4);
}

void ARM9_StartExec(struct ARM946E_S* ARM9)
{
    const u32 instr = ARM9->Instr[0].Data;
    const u8 condcode = instr >> 28;
	
	// Todo: handle IRQs
	if (CondLookup[condcode] & (1<<ARM9->Flags))
	{
    	const u16 decodebits = ((instr >> 16) & 0xFF0) | ((instr >> 4) & 0xF);

		if (ARM9_InstrLUT[decodebits])
			(ARM9_InstrLUT[decodebits])(ARM9);
		else
		{
			printf("ARM9 - UNIMPL ARM INSTR: %08X @ %08X!!!\n", ARM9->Instr[0].Data, ARM9->PC);
			for (int i = 0; i < 16; i++) printf("%i, %08X ", i, ARM9->R[i]);
			while (true)
				;
		}
		//for (int i = 0; i < 16; i++) printf("%i, %08X ", i, ARM9->R[i]);
		//printf("\n");
	}
	else if (condcode == COND_NV) // do special handling for unconditional instructions
	{
		void (*func)(struct ARM946E_S*) = ARM9_DecodeUncondInstr(instr);
		if (func) func(ARM9);
		else
		{
			printf("ARM9 - UNCOND: %08X!!!!\n", ARM9->Instr[0].Data);
			for (int i = 0; i < 16; i++) printf("%i, %08X ", i, ARM9->R[i]);
			while (true)
				;
		}
	}
	else if (false) // TODO: handle illegal BKPTs? they still execute even when their condition fails
	{
	}
	else
	{
		// instruction was not executed.
		//printf("INSTR: %08X SKIP\n", ARM9->Instr[0].Data);
	}
}

void THUMB9_StartExec(struct ARM946E_S* ARM9)
{
	const u16 instr = ARM9->Instr[0].Data;

	const u8 decodebits = instr >> 10;

	if (THUMB9_InstrLUT[decodebits])
		(THUMB9_InstrLUT[decodebits])(ARM9);
	else
	{
		printf("ARM9 - UNIMPL THUMB INSTR: %04X @ %08X\n", instr, ARM9->PC);
		while(true)
			;
	}
}

void ARM9_RunInterpreter(struct ARM946E_S* ARM9, u64 target)
{
	while (ARM9->Timestamp <= target)
	{
        //(ARM9->NextStep)(ARM9);
		if (ARM9->Halted)
		{
			
		}
		else
		{
			ARM9_StartFetch(ARM9);
			if (ARM9->Thumb) THUMB9_StartExec(ARM9);
			else ARM9_StartExec(ARM9);
		}
		ARM9->Timestamp++;
	}
}
