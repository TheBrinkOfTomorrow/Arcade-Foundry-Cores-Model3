/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2003-2026 The Supermodel Team
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

/*
 * ppc_ops.c
 *
 * PowerPC common opcodes. Included from ppc.cpp; do not compile
 * separately.
 * 
 * Changes to opcode handlers since inclusion in new Supermodel:
 *		- Feb. 13 2011: Changed stwcx. to always set the EQ flag.
 */

/* PowerPC common opcodes */

// it really seems like this should be elsewhere - like maybe the floating point checks can hang out someplace else
#include <cfenv>
#include <cmath>
// Inform the compiler that the FP environment (rounding mode, status flags) is
// accessed at runtime via fesetround(). Each compiler has its own mechanism:
// MSVC uses float_control + fenv_access; Clang supports the standard STDC pragma;
// GCC ignores STDC FENV_ACCESS and warns, so we use trapping-math (don't assume
// FP ops don't trap) and rounding-math (don't assume default rounding mode).
// Note: __clang__ must be checked before __GNUC__ since Clang defines both.
#ifdef _MSC_VER
#pragma float_control(precise,on) // because of fenv_access(on)
#pragma fenv_access(on)           // because of fesetround
#elif defined(__clang__)
#pragma STDC FENV_ACCESS ON       // because of fesetround
#elif defined(__GNUC__)
#pragma GCC optimize ("trapping-math,rounding-math") // because of fesetround
#else
#pragma STDC FENV_ACCESS ON       // because of fesetround
#endif

static void ppc_unimplemented(UINT32 op)
{
	ErrorLog("PowerPC hit an unimplemented instruction. Halting emulation until reset.");
	DebugLog("PowerPC encountered an unimplemented opcode %08X at %08X\n", op, ppc.pc);
	ppc.fatalError = true;
}

static void ppc_addx(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);

	REG(RT) = ra + rb;

	if( OEBIT ) {
		SET_ADD_OV(REG(RT), ra, rb);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_addcx(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);

	REG(RT) = ra + rb;

	SET_ADD_CA(REG(RT), ra, rb);

	if( OEBIT ) {
		SET_ADD_OV(REG(RT), ra, rb);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_addex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);
	UINT32 carry = (XER >> 29) & 0x1;
	UINT32 tmp;

	tmp = rb + carry;
	REG(RT) = ra + tmp;

	if( ADD_CA(tmp, rb, carry) || ADD_CA(REG(RT), ra, tmp) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;

	if( OEBIT ) {
		SET_ADD_OV(REG(RT), ra, rb);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_addi(UINT32 op)
{
	UINT32 i = SIMM16;
	UINT32 a = RA;

	if( a )
		i += REG(a);

	REG(RT) = i;
}

static void ppc_addic(UINT32 op)
{
	UINT32 i = SIMM16;
	UINT32 ra = REG(RA);

	REG(RT) = ra + i;

	if( ADD_CA(REG(RT), ra, i) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;
}

static void ppc_addic_rc(UINT32 op)
{
	UINT32 i = SIMM16;
	UINT32 ra = REG(RA);

	REG(RT) = ra + i;

	if( ADD_CA(REG(RT), ra, i) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;

	SET_CR0(REG(RT));
}

static void ppc_addis(UINT32 op)
{
	UINT32 i = UIMM16 << 16;
	UINT32 a = RA;

	if( a )
		i += REG(a);

	REG(RT) = i;
}

static void ppc_addmex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 carry = (XER >> 29) & 0x1;
	UINT32 tmp;

	tmp = ra + carry;
	REG(RT) = tmp + -1;

	if( ADD_CA(tmp, ra, carry) || ADD_CA(REG(RT), tmp, -1) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;

	if( OEBIT ) {
		SET_ADD_OV(REG(RT), ra, carry - 1);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_addzex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 carry = (XER >> 29) & 0x1;

	REG(RT) = ra + carry;

	if( ADD_CA(REG(RT), ra, carry) )
		XER |= XER_CA;
	else
		XER &= ~XER_CA;

	if( OEBIT ) {
		SET_ADD_OV(REG(RT), ra, carry);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_andx(UINT32 op)
{
	REG(RA) = REG(RS) & REG(RB);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_andcx(UINT32 op)
{
	REG(RA) = REG(RS) & ~REG(RB);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_andi_rc(UINT32 op)
{
	UINT32 i = UIMM16;

	REG(RA) = REG(RS) & i;

	SET_CR0(REG(RA));
}

static void ppc_andis_rc(UINT32 op)
{
	UINT32 i = UIMM16 << 16;

	REG(RA) = REG(RS) & i;

	SET_CR0(REG(RA));
}

static void ppc_bx(UINT32 op)
{
	INT32 li = op & 0x3fffffc;
	if( li & 0x2000000 )
		li |= 0xfc000000;

	ppc.npc = li;

	if( !AABIT ) {
		ppc.npc += ppc.pc;
	}

	if( LKBIT ) {
		LR = ppc.pc + 4;
	}

	ppc_change_pc(ppc.npc);
}

/*
 * Wait loops. Inside ppc_execute nothing but the PowerPC runs, so a loop that
 * only polls RAM, counts a register down or watches the timebase or the
 * decrementer cannot be ended by anything else: it runs until an interrupt is
 * taken (the decrementer's, ppc.dec_trigger_cycle, or one at the next time
 * slice) or until its own condition changes, which for the timebase and the
 * countdown is a known icount. On a taken branch back to such a loop, whole
 * iterations are skipped up to the first of those and the interpreter runs
 * the rest; the registers the skipped iterations wrote are set to what the
 * last of them left, so icount, the timebase, the decrementer and every
 * register come out exactly as without the skip.
 *
 *   lis rA,hi ; lbz rA,lo(rA) ; cmp/cmpl crN,rA,rB ; bc on crN -12
 *                                                              RAM byte poll
 *   lbz/lhz/lwz rA,d(rB) ; cmp/cmpl/cmpi/cmpli crN,rA,.. ; bc on crN -8
 *   cmp/cmpl/cmpi/cmpli crN,rA,.. ; lbz/lhz/lwz rA,d(rB) ; bc on crN -8
 *                                                              RAM polls
 *   mfspr rA,dec ; cmpi cr0,rA,0 ; blt -8                      decrementer < 0
 *   addic. rA,rA,-1 ; bne -4                                   countdown
 *   mftb rA ; rlwinm. rB,rA,0,b,b ; beq -8                     timebase bit b
 *   mftb rA ; cmpl crN,rB,rA ; bt cr0[x],.. ; bt crN[gt] -12   timebase < rB
 *
 * PPC_SKIP is a mask of the kinds to skip (1 RAM polls, 2 decrementer,
 * 4 countdown, 8 timebase; default all), 0 turns it off;
 * -DPPC_NO_IDLE_SKIP leaves it out, -DPPC_SKIP_VERIFY checks it.
 */
static int ppc_skip_mask = -1;

// Iterations of an n-instruction loop that can be skipped from its head,
// where icount will be `at`: icount must stay above 0 at each instruction and
// must not pass through the decrementer's trigger (checked after every one).
static inline INT32 ppc_skippable(INT32 at, INT32 n)
{
	INT32 room = at;
	if (ppc.dec_trigger_cycle <= at && at - ppc.dec_trigger_cycle - 1 < room)
		room = at - ppc.dec_trigger_cycle - 1;
	return room > 0 ? room / n : 0;
}

// The timebase an mftb executed at icount `i` reads (ppc_read_timebase).
static inline UINT64 ppc_timebase_at(INT32 i)
{
	return ppc.tb + (UINT64)((ppc.tb_base_icount - i) / ppc.timer_ratio);
}

// Iterations of an n-instruction loop with an mftb first, starting at icount
// `at`, whose mftb reads less than `target`.
static inline INT32 ppc_timebase_below(INT32 at, INT32 n, UINT64 target)
{
	if (target <= ppc.tb)
		return 0;
	INT64 d = (INT64)(target - ppc.tb) * ppc.timer_ratio - (INT64)(ppc.tb_base_icount - at);
	if (d <= 0)
		return 0;
	INT64 j = (d + n - 1) / n;
	return j > 0x7FFFFFFF ? 0x7FFFFFFF : (INT32)j;
}

#ifdef PPC_SKIP_VERIFY
#define PPC_SKIP_TO(n, k, addr)	do { if ((k) > 0 && !ppc_verify_until) { ppc_verify_head = head; ppc_verify_addr = (addr); ppc_verify_until = at - (n) * (k); } } while (0)
#else
#define PPC_SKIP_TO(n, k, addr)	(ppc.icount -= (n) * (k))
#endif

static inline UINT32 ppc_cmp_field(UINT32 a, UINT32 b, int is_signed)
{
	UINT32 c = is_signed ? ((INT32)a < (INT32)b ? 0x8 : (INT32)a > (INT32)b ? 0x4 : 0x2)
	                     : (a < b ? 0x8 : a > b ? 0x4 : 0x2);
	return c | ((XER & XER_SO) ? 1 : 0);
}

static inline void ppc_skip_wait_loop(UINT32 op)
{
	if (ppc_skip_mask < 0)
	{
		const char *e = getenv("PPC_SKIP");
		ppc_skip_mask = e ? atoi(e) : 15;
	}
	UINT32 head = ppc.npc;
	if (head + 8 - ppc.cur_fetch.start > ppc.cur_fetch.end - ppc.cur_fetch.start)
		return;
	INT32 at = ppc.icount - 1;	// icount at the loop head, once this branch is counted
	UINT32 bop = op & ~0x00200000;	// the branch without its prediction bit
	// bc -8 on a bit of any CR field, taken if true or if false (BO 12/13, 4/5)
	int poll8 = (op & 0xFC00FFFF) == 0x4000FFF8 && (((op >> 21) & 0x1E) == 12 || ((op >> 21) & 0x1E) == 4);
	int poll12 = (op & 0xFC00FFFF) == 0x4000FFF4 && (((op >> 21) & 0x1E) == 12 || ((op >> 21) & 0x1E) == 4);
	UINT32 bcrf = (op >> 18) & 7;	// the CR field it tests
	UINT32 w0 = ppc.op[0], w1 = ppc.op[1], w2 = ppc.op[2];

	if ((ppc_skip_mask & 1) && poll12 && (w0 >> 26) == 15)
	{
		// lis rA,hi ; lbz rA,lo(rA) ; cmp/cmpl crN,rA,rB ; bc on crN -12
		UINT32 ra = (w0 >> 21) & 31;
		if ((w0 >> 26) != 15 || ((w0 >> 16) & 31) != 0)
			return;
		if ((w1 >> 26) != 34 || ((w1 >> 21) & 31) != ra || ((w1 >> 16) & 31) != ra)
			return;
		UINT32 addr = (w0 << 16) + (UINT32)(INT32)(INT16)w1;
		if (addr >= 0x800000)
			return;
		if ((w2 >> 26) != 31 || (((w2 >> 1) & 0x3FF) != 32 && ((w2 >> 1) & 0x3FF) != 0)
		    || ((w2 >> 23) & 7) != bcrf || ((w2 >> 21) & 3) != 0)	// into the field tested, L = 0
			return;
		UINT32 x = (w2 >> 16) & 31, y = (w2 >> 11) & 31;
		if (!((x == ra) ^ (y == ra)))
			return;
		// The value just compared must still be in RAM: an interrupt taken
		// between the lbz and this branch can have changed it.
		if (REG(ra) != READ8(addr))
			return;
		INT32 k = ppc_skippable(at, 4);
		PPC_SKIP_TO(4, k, addr);
	}
	else if ((ppc_skip_mask & 1) && poll8
	         && ((w0 >> 26) == 34 || (w0 >> 26) == 40 || (w0 >> 26) == 32))
	{
		// lbz/lhz/lwz rA,d(rB) ; cmp/cmpl/cmpi/cmpli crN,rA,.. ; bc on crN -8
		UINT32 load = w0 >> 26, ra = (w0 >> 21) & 31, rb = (w0 >> 16) & 31;
		if ((load != 34 && load != 40 && load != 32) || ra == rb)
			return;
		UINT32 addr = (rb ? REG(rb) : 0) + (UINT32)(INT32)(INT16)w0;
		UINT32 size = load == 34 ? 1 : load == 40 ? 2 : 4;
		if (addr >= 0x800000 || (addr & (size - 1)))
			return;
		UINT32 c = w1 >> 26;
		if (((w1 >> 23) & 7) != bcrf || ((w1 >> 21) & 3) != 0)	// into the field tested, L = 0
			return;
		if (c == 31)
		{
			UINT32 xo = (w1 >> 1) & 0x3FF, x = (w1 >> 16) & 31, y = (w1 >> 11) & 31;
			if ((xo != 0 && xo != 32) || !((x == ra) ^ (y == ra)) || x == rb || y == rb)
				return;
		}
		else if (c == 10 || c == 11)
		{
			if (((w1 >> 16) & 31) != ra)
				return;
		}
		else
			return;
		UINT32 now = size == 1 ? READ8(addr) : size == 2 ? READ16(addr) : READ32(addr);
		if (REG(ra) != now)
			return;
		INT32 k = ppc_skippable(at, 3);
		PPC_SKIP_TO(3, k, addr);
	}
	else if ((ppc_skip_mask & 1) && poll8
	         && ((w1 >> 26) == 34 || (w1 >> 26) == 40 || (w1 >> 26) == 32))
	{
		// cmp/cmpl/cmpi/cmpli crN,rA,.. ; lbz/lhz/lwz rA,d(rB) ; bc on crN -8
		// (the RAM poll with the compare first: it compares the value loaded
		// in the pass before)
		UINT32 load = w1 >> 26, ra = (w1 >> 21) & 31, rb = (w1 >> 16) & 31;
		if (ra == rb)
			return;
		UINT32 addr = (rb ? REG(rb) : 0) + (UINT32)(INT32)(INT16)w1;
		UINT32 size = load == 34 ? 1 : load == 40 ? 2 : 4;
		if (addr >= 0x800000 || (addr & (size - 1)))
			return;
		UINT32 c = w0 >> 26;
		if (((w0 >> 23) & 7) != bcrf || ((w0 >> 21) & 3) != 0)	// into the field tested, L = 0
			return;
		if (c == 31)
		{
			UINT32 xo = (w0 >> 1) & 0x3FF, x = (w0 >> 16) & 31, y = (w0 >> 11) & 31;
			if ((xo != 0 && xo != 32) || !((x == ra) ^ (y == ra)) || x == rb || y == rb)
				return;
		}
		else if (c == 10 || c == 11)
		{
			if (((w0 >> 16) & 31) != ra)
				return;
		}
		else
			return;
		// The branch was taken on the value loaded before this pass's load;
		// the loop is stable once the compare would give the same result
		// again, which holds when RAM still holds the value just loaded and
		// that value compares as the one before it did (the field unchanged).
		UINT32 now = size == 1 ? READ8(addr) : size == 2 ? READ16(addr) : READ32(addr);
		if (REG(ra) != now)
			return;
		UINT32 crv = CR(bcrf);
		UINT32 cmpd;
		if (c == 31)
		{
			UINT32 x = (w0 >> 16) & 31, y = (w0 >> 11) & 31;
			cmpd = ppc_cmp_field(REG(x), REG(y), ((w0 >> 1) & 0x3FF) == 0);
		}
		else
			cmpd = ppc_cmp_field(REG(ra), c == 11 ? (UINT32)(INT32)(INT16)w0 : (UINT32)(w0 & 0xFFFF), c == 11);
		if (cmpd != crv)
			return;
		INT32 k = ppc_skippable(at, 3);
		PPC_SKIP_TO(3, k, addr);
	}
	else if ((ppc_skip_mask & 2) && bop == 0x4180FFF8)
	{
		// mfspr rA,dec ; cmpi cr0,rA,0 ; blt -8
		UINT32 ra = (w0 >> 21) & 31;
		if ((w0 & 0xFC1FFFFF) != 0x7C1602A6 || w1 != (0x2C000000 | (ra << 16)))
			return;
		INT32 k = ppc_skippable(at, 3);
		if (k <= 0)
			return;
		// DEC only falls: it must still read negative at the last skipped mfspr
		// (past 0x80000000 it reads positive and the loop ends by itself).
		INT32 last = at - 3 * (k - 1);
		INT64 dec_then = (INT64)(INT32)DEC - (INT64)((ppc.dec_base_icount - last) / ppc.timer_ratio);
		if ((INT32)read_decrementer() >= 0 || dec_then < INT32_MIN)
			return;
		PPC_SKIP_TO(3, k, 0);
#ifndef PPC_SKIP_VERIFY
		REG(ra) = (UINT32)dec_then;	// what the last skipped mfspr read (cr0 stays lt)
#endif
	}
	else if ((ppc_skip_mask & 4) && bop == 0x4082FFFC)
	{
		// addic. rA,rA,-1 ; bne -4: v more passes, the last one leaves.
		UINT32 ra = (w0 >> 21) & 31;
		if (w0 != (0x3400FFFF | (ra << 21) | (ra << 16)))
			return;
		UINT32 v = REG(ra);
		INT32 k = ppc_skippable(at, 2);
		if ((UINT32)k > v - 1)
			k = (INT32)(v - 1);
		if (k <= 0)
			return;
		PPC_SKIP_TO(2, k, 0);
#ifndef PPC_SKIP_VERIFY
		REG(ra) = v - (UINT32)k;
		XER |= XER_CA;	// each pass added -1 to a non-zero value
		SET_CR0(REG(ra));
#endif
	}
	else if ((ppc_skip_mask & 8) && bop == 0x4182FFF8 && (w0 & 0xFC1FFFFF) == 0x7C0C42E6)
	{
		// mftb rA ; rlwinm. rB,rA,0,b,b ; beq -8: until timebase bit b is set
		UINT32 ra = (w0 >> 21) & 31, rb = (w1 >> 16) & 31;
		UINT32 mb = (w1 >> 6) & 31, me = (w1 >> 1) & 31;
		if ((w1 >> 26) != 21 || ((w1 >> 21) & 31) != ra || ((w1 >> 11) & 31) != 0 || mb != me || !(w1 & 1))
			return;
		UINT64 m = 0x80000000u >> mb;
		UINT64 t = ppc_timebase_at(at);
		if (t & m)
			return;
		UINT64 target = (t & ~(2 * m - 1)) + m;
		INT32 k = ppc_timebase_below(at, 3, target);
		INT32 room = ppc_skippable(at, 3);
		if (k > room)
			k = room;
		if (k <= 0)
			return;
		PPC_SKIP_TO(3, k, 0);
#ifndef PPC_SKIP_VERIFY
		REG(ra) = (UINT32)ppc_timebase_at(at - 3 * (k - 1));
		REG(rb) = 0;	// cr0 stays eq
#endif
	}
	else if ((ppc_skip_mask & 8) && (bop & 0xFFE3FFFF) == 0x4181FFF4 && (w0 & 0xFC1FFFFF) == 0x7C0C42E6)
	{
		// mftb rA ; cmpl crN,0,rB,rA ; bt cr0[x],.. (not taken) ; bt crN[gt] -12:
		// while rB > the timebase's low word
		UINT32 ra = (w0 >> 21) & 31, crf = (op >> 18) & 7;
		if ((w1 & 0xFC6007FE) != 0x7C000040 || ((w1 >> 23) & 7) != crf || ((w1 >> 11) & 31) != ra)
			return;
		UINT32 rb = (w1 >> 16) & 31;
		if (rb == ra || (w2 >> 26) != 16 || (w2 & 3) || ((w2 >> 21) & 0x1E) != 12)	// bc, BO 12/13, no link/abs
			return;
		UINT32 bi = (w2 >> 16) & 31;
		if (bi / 4 == crf || CRBIT(bi))
			return;
		UINT64 t = ppc_timebase_at(at);
		if ((UINT32)t >= REG(rb))
			return;
		UINT64 target = (t & ~(UINT64)0xFFFFFFFF) | REG(rb);
		INT32 k = ppc_timebase_below(at, 4, target);
		INT32 room = ppc_skippable(at, 4);
		if (k > room)
			k = room;
		if (k <= 0)
			return;
		PPC_SKIP_TO(4, k, 0);
#ifndef PPC_SKIP_VERIFY
		REG(ra) = (UINT32)ppc_timebase_at(at - 4 * (k - 1));
		CR(crf) = ppc_cmp_field(REG(rb), REG(ra), 0);
#endif
	}
}

static void ppc_bcx(UINT32 op)
{
	int condition = check_condition_code(BO, BI);

	if( condition ) {
		ppc.npc = SIMM16 & ~0x3;
		if( !AABIT )
			ppc.npc += ppc.pc;

		ppc_change_pc(ppc.npc);
#ifndef PPC_NO_IDLE_SKIP
		if ((INT16)op < 0 && ppc.npc >= ppc.pc - 12)
			ppc_skip_wait_loop(op);
#endif
	}

	if( LKBIT ) {
		LR = ppc.pc + 4;
	}
}

static void ppc_bcctrx(UINT32 op)
{
	int condition = check_condition_code(BO, BI);

	if( condition ) {
		ppc.npc = CTR & ~0x3;
		ppc_change_pc(ppc.npc);
	}

	if( LKBIT ) {
		LR = ppc.pc + 4;
	}
}

static void ppc_bclrx(UINT32 op)
{
	int condition = check_condition_code(BO, BI);

	if( condition ) {
		ppc.npc = LR & ~0x3;
		ppc_change_pc(ppc.npc);
	}

	if( LKBIT ) {
		LR = ppc.pc + 4;
	}
}

static void ppc_cmp(UINT32 op)
{
	INT32 ra = REG(RA);
	INT32 rb = REG(RB);
	int d = CRFD;

	if( ra < rb )
		CR(d) = 0x8;
	else if( ra > rb )
		CR(d) = 0x4;
	else
		CR(d) = 0x2;

	if( XER & XER_SO )
		CR(d) |= 0x1;
}

static void ppc_cmpi(UINT32 op)
{
	INT32 ra = REG(RA);
	INT32 i = SIMM16;
	int d = CRFD;

	if( ra < i )
		CR(d) = 0x8;
	else if( ra > i )
		CR(d) = 0x4;
	else
		CR(d) = 0x2;

	if( XER & XER_SO )
		CR(d) |= 0x1;
}

static void ppc_cmpl(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);
	int d = CRFD;

	if( ra < rb )
		CR(d) = 0x8;
	else if( ra > rb )
		CR(d) = 0x4;
	else
		CR(d) = 0x2;

	if( XER & XER_SO )
		CR(d) |= 0x1;
}

static void ppc_cmpli(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 i = UIMM16;
	int d = CRFD;

	if( ra < i )
		CR(d) = 0x8;
	else if( ra > i )
		CR(d) = 0x4;
	else
		CR(d) = 0x2;

	if( XER & XER_SO )
		CR(d) |= 0x1;
}

static void ppc_cntlzw(UINT32 op)
{
	int n = 0;
	int t = RT;
	UINT32 m = 0x80000000;

	while(n < 32)
	{
		if( REG(t) & m )
			break;
		m >>= 1;
		n++;
	}

	REG(RA) = n;

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_crand(UINT32 op)
{
	int bit = RT;
	int b = CRBIT(RA) & CRBIT(RB);
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_crandc(UINT32 op)
{
	int bit = RT;
	int b = CRBIT(RA) & (CRBIT(RB) ^ 0x1);
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_creqv(UINT32 op)
{
	int bit = RT;
	int b = (CRBIT(RA) ^ CRBIT(RB)) ^ 0x1;
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_crnand(UINT32 op)
{
	int bit = RT;
	int b = (CRBIT(RA) & CRBIT(RB)) ^ 0x1;
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_crnor(UINT32 op)
{
	int bit = RT;
	int b = (CRBIT(RA) | CRBIT(RB)) ^ 0x1;
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_cror(UINT32 op)
{
	int bit = RT;
	int b = CRBIT(RA) | CRBIT(RB);
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_crorc(UINT32 op)
{
	int bit = RT;
	int b = CRBIT(RA) | (CRBIT(RB) ^ 0x1);
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_crxor(UINT32 op)
{
	int bit = RT;
	int b = CRBIT(RA) ^ CRBIT(RB);
	if( b )
		CR(bit / 4) |= _BIT(3-(bit % 4));
	else
		CR(bit / 4) &= ~_BIT(3-(bit % 4));
}

static void ppc_dcbf(UINT32 op)
{

}

static void ppc_dcbi(UINT32 op)
{

}

static void ppc_dcbst(UINT32 op)
{

}

static void ppc_dcbt(UINT32 op)
{

}

static void ppc_dcbtst(UINT32 op)
{

}

static void ppc_dcbz(UINT32 op)
{
	// TODO (verified via spec, not from MAME): should zero the 32-byte cache block containing (rA|0)+rB. Left a no-op until a game is known
	// to use it, as a wrong implementation would clear memory
}

static void ppc_divwx(UINT32 op)
{
	if( REG(RB) == 0 && REG(RA) < 0x80000000 )
	{
		REG(RT) = 0;
		if( OEBIT ) {
			XER |= XER_SO | XER_OV;
		}
	}
	else if( REG(RB) == 0 || (REG(RB) == 0xffffffff && REG(RA) == 0x80000000) )
	{
		REG(RT) = 0xffffffff;
		if( OEBIT ) {
			XER |= XER_SO | XER_OV;
		}
	}
	else
	{
		REG(RT) = (INT32)REG(RA) / (INT32)REG(RB);
		if( OEBIT ) {
			XER &= ~XER_OV;
		}
	}

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_divwux(UINT32 op)
{
	if( REG(RB) == 0 )
	{
		REG(RT) = 0;
		if( OEBIT ) {
			XER |= XER_SO | XER_OV;
		}
	}
	else
	{
		REG(RT) = (UINT32)REG(RA) / (UINT32)REG(RB);
		if( OEBIT ) {
			XER &= ~XER_OV;
		}
	}

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_eieio(UINT32 op)
{

}

static void ppc_eqvx(UINT32 op)
{
	REG(RA) = ~(REG(RS) ^ REG(RB));

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_extsbx(UINT32 op)
{
	REG(RA) = (INT32)(INT8)REG(RS);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_extshx(UINT32 op)
{
	REG(RA) = (INT32)(INT16)REG(RS);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_icbi(UINT32 op)
{

}

static void ppc_isync(UINT32 op)
{

}

static void ppc_lbz(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (UINT32)READ8(ea);
}

static void ppc_lbzu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	REG(RT) = (UINT32)READ8(ea);
	REG(RA) = ea;
}

static void ppc_lbzux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	REG(RT) = (UINT32)READ8(ea);
	REG(RA) = ea;
}

static void ppc_lbzx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (UINT32)READ8(ea);
}

static void ppc_lha(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (INT32)(INT16)READ16(ea);
}

static void ppc_lhau(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	REG(RT) = (INT32)(INT16)READ16(ea);
	REG(RA) = ea;
}

static void ppc_lhaux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	REG(RT) = (INT32)(INT16)READ16(ea);
	REG(RA) = ea;
}

static void ppc_lhax(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (INT32)(INT16)READ16(ea);
}

static void ppc_lhbrx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT16 w;

	if( RA != 0 )
		ea += REG(RA);

	w = READ16(ea);
	REG(RT) = (UINT32)BYTE_REVERSE16(w);
}

static void ppc_lhz(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (UINT32)READ16(ea);
}

static void ppc_lhzu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	REG(RT) = (UINT32)READ16(ea);
	REG(RA) = ea;
}

static void ppc_lhzux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	REG(RT) = (UINT32)READ16(ea);
	REG(RA) = ea;
}

static void ppc_lhzx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = (UINT32)READ16(ea);
}

static void ppc_lmw(UINT32 op)
{
	int r = RT;
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	while( r <= 31 )
	{
		REG(r) = READ32(ea);
		ea += 4;
		r++;
	}
}

static void ppc_lswi(UINT32 op)
{
	int n, r, i;
	UINT32 ea = (RA != 0) ? REG(RA) : 0;

	n = (RB == 0) ? 32 : RB;

	r = RT - 1;
	i = 0;

	while(n > 0)
	{
		if (i == 0) {
			r = (r + 1) % 32;
			REG(r) = 0;
		}
		REG(r) |= ((READ8(ea) & 0xff) << (24 - i));
		i += 8;
		if (i == 32) {
			i = 0;
		}
		ea++;
		n--;
	}
}

static void ppc_lswx(UINT32 op)
{
	int n, r, i;
	UINT32 ea = REG(RB);
	if(RA != 0)
		ea += REG(RA);

	n = ppc.xer & 0x7f;

	r = RT - 1;
	i = 0;

	while(n > 0)
	{
		if (i == 0) {
			r = (r + 1) % 32;
			REG(r) = 0;
		}
		REG(r) |= ((READ8(ea) & 0xff) << (24 - i));
		i += 8;
		if (i == 32) {
			i = 0;
		}
		ea++;
		n--;
	}
}

static void ppc_lwarx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	ppc.reserved_address = ea;
	ppc.reserved = 1;

	REG(RT) = READ32(ea);
}

static void ppc_lwbrx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 w;

	if( RA != 0 )
		ea += REG(RA);

	w = READ32(ea);
	REG(RT) = BYTE_REVERSE32(w);
}

static void ppc_lwz(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = READ32(ea);
}

static void ppc_lwzu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	REG(RT) = READ32(ea);
	REG(RA) = ea;
}

static void ppc_lwzux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	REG(RT) = READ32(ea);
	REG(RA) = ea;
}

static void ppc_lwzx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	REG(RT) = READ32(ea);
}

static void ppc_mcrf(UINT32 op)
{
	CR(RT >> 2) = CR(RA >> 2);
}

static void ppc_mcrxr(UINT32 op)
{
	CR(RT >> 2) = (XER >> 28) & 0x0F;
	XER &= ~0xf0000000;
}

static void ppc_mfcr(UINT32 op)
{
	REG(RT) = ppc_get_cr();
}

static void ppc_mfmsr(UINT32 op)
{
	REG(RT) = ppc_get_msr();
}

static void ppc_mfspr(UINT32 op)
{
	REG(RT) = ppc_get_spr(SPR);
}

static void ppc_mtcrf(UINT32 op)
{
	int fxm = FXM;
	int t = RT;

	if( fxm & 0x80 )	CR(0) = (REG(t) >> 28) & 0xf;
	if( fxm & 0x40 )	CR(1) = (REG(t) >> 24) & 0xf;
	if( fxm & 0x20 )	CR(2) = (REG(t) >> 20) & 0xf;
	if( fxm & 0x10 )	CR(3) = (REG(t) >> 16) & 0xf;
	if( fxm & 0x08 )	CR(4) = (REG(t) >> 12) & 0xf;
	if( fxm & 0x04 )	CR(5) = (REG(t) >> 8) & 0xf;
	if( fxm & 0x02 )	CR(6) = (REG(t) >> 4) & 0xf;
	if( fxm & 0x01 )	CR(7) = (REG(t) >> 0) & 0xf;
}

static void ppc_mtmsr(UINT32 op)
{
	ppc_set_msr(REG(RS));
}

static void ppc_mtspr(UINT32 op)
{
	ppc_set_spr(SPR, REG(RS));
}

static void ppc_mulhwx(UINT32 op)
{
	INT64 ra = (INT64)(INT32)REG(RA);
	INT64 rb = (INT64)(INT32)REG(RB);

	REG(RT) = (UINT32)((ra * rb) >> 32);

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_mulhwux(UINT32 op)
{
	UINT64 ra = (UINT64)REG(RA);
	UINT64 rb = (UINT64)REG(RB);

	REG(RT) = (UINT32)((ra * rb) >> 32);

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_mulli(UINT32 op)
{
	REG(RT) = REG(RA) * (UINT32)SIMM16;	// verified via spec, not from MAME: unsigned, as signed overflow is undefined
}

static void ppc_mullwx(UINT32 op)
{
	INT64 ra = (INT64)(INT32)REG(RA);
	INT64 rb = (INT64)(INT32)REG(RB);
	INT64 r;

	r = ra * rb;
	REG(RT) = (UINT32)r;

	if( OEBIT ) {
		XER &= ~XER_OV;

		if( r != (INT64)(INT32)r )
			XER |= XER_OV | XER_SO;
	}

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_nandx(UINT32 op)
{
	REG(RA) = ~(REG(RS) & REG(RB));

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_negx(UINT32 op)
{
	REG(RT) = 0 - REG(RA);	// verified via spec, not from MAME: unsigned, as -INT_MIN is undefined

	if( OEBIT ) {
		if( REG(RT) == 0x80000000 )
			XER |= XER_OV | XER_SO;
		else
			XER &= ~XER_OV;
	}

	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_norx(UINT32 op)
{
	REG(RA) = ~(REG(RS) | REG(RB));

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_orx(UINT32 op)
{
	REG(RA) = REG(RS) | REG(RB);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_orcx(UINT32 op)
{
	REG(RA) = REG(RS) | ~REG(RB);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_ori(UINT32 op)
{
	REG(RA) = REG(RS) | UIMM16;
}

static void ppc_oris(UINT32 op)
{
	REG(RA) = REG(RS) | (UIMM16 << 16);
}

static void ppc_rfi(UINT32 op)
{
	UINT32 msr;
	ppc.npc = ppc_get_spr(SPR_SRR0) & 0xFFFFFFFC;	// from MAME: rfi ignores SRR0[30:31]
	msr = ppc_get_spr(SPR_SRR1);	// TODO: like MAME, restore only the MSR bits 0x87C0FFFF from SRR1 (check against the 603e manual)
	ppc_set_msr( msr );

	ppc_change_pc(ppc.npc);
}

static void ppc_rlwimix(UINT32 op)
{
	UINT32 r;
	UINT32 mask = GET_ROTATE_MASK(MB, ME);
	UINT32 rs = REG(RS);
	int sh = SH;

	r = (rs << sh) | (rs >> ((32-sh) & 31));	// verified via spec, not from MAME: & 31, as a shift by 32 (sh = 0) is undefined
	REG(RA) = (REG(RA) & ~mask) | (r & mask);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_rlwinmx(UINT32 op)
{
	UINT32 r;
	UINT32 mask = GET_ROTATE_MASK(MB, ME);
	UINT32 rs = REG(RS);
	int sh = SH;

	r = (rs << sh) | (rs >> ((32-sh) & 31));	// verified via spec, not from MAME: & 31, as a shift by 32 (sh = 0) is undefined
	REG(RA) = r & mask;

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_rlwnmx(UINT32 op)
{
	UINT32 r;
	UINT32 mask = GET_ROTATE_MASK(MB, ME);
	UINT32 rs = REG(RS);
	int sh = REG(RB) & 0x1f;

	r = (rs << sh) | (rs >> ((32-sh) & 31));	// verified via spec, not from MAME: & 31, as a shift by 32 (sh = 0) is undefined
	REG(RA) = r & mask;

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_sc(UINT32 op)
{
	ppc603_exception(EXCEPTION_SYSTEM_CALL);
}

static void ppc_slwx(UINT32 op)
{
	int sh = REG(RB) & 0x3f;

	if( sh > 31 ) {
		REG(RA) = 0;
	}
	else {
		REG(RA) = REG(RS) << sh;
	}

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_srawx(UINT32 op)
{
	int sh = REG(RB) & 0x3f;
	UINT32 rs = REG(RS);	// verified via spec, not from MAME: rA may be rS, so read it before writing rA

	XER &= ~XER_CA;

	if( sh > 31 ) {
		if (rs & 0x80000000)
			REG(RA) = 0xffffffff;
		else
			REG(RA) = 0;
		if( REG(RA) )
			XER |= XER_CA;
	}
	else {
		REG(RA) = (INT32)rs >> sh;
		if( ((INT32)rs < 0) && (rs & BITMASK_0(sh)) )
			XER |= XER_CA;
	}

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_srawix(UINT32 op)
{
	int sh = SH;

	XER &= ~XER_CA;
	if( ((INT32)(REG(RS)) < 0) && (REG(RS) & BITMASK_0(sh)) )
		XER |= XER_CA;

	REG(RA) = (INT32)(REG(RS)) >> sh;

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_srwx(UINT32 op)
{
	int sh = REG(RB) & 0x3f;

	if( sh > 31 ) {
		REG(RA) = 0;
	}
	else {
		REG(RA) = REG(RS) >> sh;
	}

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_stb(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	WRITE8(ea, (UINT8)REG(RS));
}

static void ppc_stbu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	WRITE8(ea, (UINT8)REG(RS));
	REG(RA) = ea;
}

static void ppc_stbux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	WRITE8(ea, (UINT8)REG(RS));
	REG(RA) = ea;
}

static void ppc_stbx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	WRITE8(ea, (UINT8)REG(RS));
}

static void ppc_sth(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	WRITE16(ea, (UINT16)REG(RS));
}

static void ppc_sthbrx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT16 w;

	if( RA != 0 )
		ea += REG(RA);

	w = REG(RS);
	WRITE16(ea, (UINT16)BYTE_REVERSE16(w));
}

static void ppc_sthu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	WRITE16(ea, (UINT16)REG(RS));
	REG(RA) = ea;
}

static void ppc_sthux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	WRITE16(ea, (UINT16)REG(RS));
	REG(RA) = ea;
}

static void ppc_sthx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	WRITE16(ea, (UINT16)REG(RS));
}

static void ppc_stmw(UINT32 op)
{
	UINT32 ea = SIMM16;
	int r = RS;

	if( RA != 0 )
		ea += REG(RA);

	while( r <= 31 )
	{
		WRITE32(ea, REG(r));
		ea += 4;
		r++;
	}
}

static void ppc_stswi(UINT32 op)
{
	int n, r, i;
	UINT32 ea = (RA != 0) ? REG(RA) : 0;

	n = (RB == 0) ? 32 : RB;

	r = RT - 1;
	i = 0;

	while(n > 0)
	{
		if (i == 0) {
			r = (r + 1) % 32;
		}
		WRITE8(ea, (REG(r) >> (24-i)) & 0xff);
		i += 8;
		if (i == 32) {
			i = 0;
		}
		ea++;
		n--;
	}
}

static void ppc_stswx(UINT32 op)
{
	int n, r, i;
	UINT32 ea = REG(RB);
	if (RA != 0)
		ea += REG(RA);

	n = ppc.xer & 0x7f;

	r = RT - 1;
	i = 0;

	while(n > 0)
	{
		if (i == 0) {
			r = (r + 1) % 32;
		}
		WRITE8(ea, (REG(r) >> (24-i)) & 0xff);
		i += 8;
		if (i == 32) {
			i = 0;
		}
		ea++;
		n--;
	}
}

static void ppc_stw(UINT32 op)
{
	UINT32 ea = SIMM16;

	if( RA != 0 )
		ea += REG(RA);

	WRITE32(ea, REG(RS));
}

static void ppc_stwbrx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 w;

	if( RA != 0 )
		ea += REG(RA);

	w = REG(RS);
	WRITE32(ea, BYTE_REVERSE32(w));
}

static void ppc_stwcx_rc(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	if( ppc.reserved ) {
		WRITE32(ea, REG(RS));

		ppc.reserved = 0;
		ppc.reserved_address = 0;

		CR(0) = 0x2;	// set EQ to indicate success
	} else {
		CR(0) = 0;
	}

	if (XER & XER_SO)
		CR(0) |= 0x1;
}

static void ppc_stwu(UINT32 op)
{
	UINT32 ea = REG(RA) + SIMM16;

	WRITE32(ea, REG(RS));
	REG(RA) = ea;
}

static void ppc_stwux(UINT32 op)
{
	UINT32 ea = REG(RA) + REG(RB);

	WRITE32(ea, REG(RS));
	REG(RA) = ea;
}

static void ppc_stwx(UINT32 op)
{
	UINT32 ea = REG(RB);

	if( RA != 0 )
		ea += REG(RA);

	WRITE32(ea, REG(RS));
}

static void ppc_subfx(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);
	REG(RT) = rb - ra;

	if( OEBIT ) {
		SET_SUB_OV(REG(RT), rb, ra);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_subfcx(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);
	REG(RT) = rb - ra;

	SET_SUB_CA(REG(RT), rb, ra);

	if( OEBIT ) {
		SET_SUB_OV(REG(RT), rb, ra);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_subfex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 rb = REG(RB);
	UINT32 carry = (XER >> 29) & 0x1;
	UINT32 r = ~ra + carry;
	REG(RT) = rb + r;

	SET_ADD_CA(r, ~ra, carry);		/* step 1 carry */
	if( REG(RT) < r )				/* step 2 carry */
		XER |= XER_CA;

	if( OEBIT ) {
		SET_SUB_OV(REG(RT), rb, ra);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_subfic(UINT32 op)
{
	UINT32 i = SIMM16;
	UINT32 ra = REG(RA);

	REG(RT) = i - ra;

	SET_SUB_CA(REG(RT), i, ra);
}

static void ppc_subfmex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 carry = (XER >> 29) & 0x1;
	UINT32 r = ~ra + carry;
	REG(RT) = r - 1;

	SET_ADD_CA(r, ~ra, carry);		/* step 1 carry (verified via spec, not from MAME: was SET_SUB_CA, but ~ra + carry is an add) */
	if( REG(RT) < r )
		XER |= XER_CA;				/* step 2 carry */

	if( OEBIT ) {
		SET_SUB_OV(REG(RT), -1, ra);
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_subfzex(UINT32 op)
{
	UINT32 ra = REG(RA);
	UINT32 carry = (XER >> 29) & 0x1;

	REG(RT) = ~ra + carry;

	SET_ADD_CA(REG(RT), ~ra, carry);

	if( OEBIT ) {
		SET_SUB_OV(REG(RT), 0, ra);	// verified via spec, not from MAME: rD may be rA, so use the saved rA
	}
	if( RCBIT ) {
		SET_CR0(REG(RT));
	}
}

static void ppc_sync(UINT32 op)
{

}

static void ppc_tw(UINT32 op)
{
	int exception = 0;
	INT32 a = REG(RA);
	INT32 b = REG(RB);
	int to = RT;

	if(( (a < b) && (to & 0x10) ) ||
	   ( (a > b) && (to & 0x08) ) ||
	   ( (a == b) && (to & 0x04) ) ||
	   ( ((UINT32)a < (UINT32)b) && (to & 0x02) ) ||
	   ( ((UINT32)a > (UINT32)b) && (to & 0x01) )) {
		exception = 1;
	}

	if (exception) {
		ppc603_exception(EXCEPTION_TRAP);
	}
}

static void ppc_twi(UINT32 op)
{
	int exception = 0;
	INT32 a = REG(RA);
	INT32 i = SIMM16;
	int to = RT;

	if(( (a < i) && (to & 0x10) ) ||
	   ( (a > i) && (to & 0x08) ) ||
	   ( (a == i) && (to & 0x04) ) ||
	   ( ((UINT32)a < (UINT32)i) && (to & 0x02) ) ||
	   ( ((UINT32)a > (UINT32)i) && (to & 0x01) )) {
		exception = 1;
	}

	if (exception) {
		ppc603_exception(EXCEPTION_TRAP);
	}
}

static void ppc_xorx(UINT32 op)
{
	REG(RA) = REG(RS) ^ REG(RB);

	if( RCBIT ) {
		SET_CR0(REG(RA));
	}
}

static void ppc_xori(UINT32 op)
{
	REG(RA) = REG(RS) ^ UIMM16;
}

static void ppc_xoris(UINT32 op)
{
	REG(RA) = REG(RS) ^ (UIMM16 << 16);
}



static void ppc_invalid(UINT32 op)
{
	ErrorLog("PowerPC hit an invalid instruction. Halting emulation until reset.");
	DebugLog("ppc: Invalid opcode %08X PC : %X, %08X\n", op, ppc.pc, ppc.npc);
	ppc.fatalError = true;
}



#define DOUBLE_SIGN		(0x8000000000000000ULL)
#define DOUBLE_EXP		(0x7ff0000000000000ULL)
#define DOUBLE_FRAC		(0x000fffffffffffffULL)
#define DOUBLE_ZERO		(0ULL)

/*
  Floating point operations.
*/

/* UNUSED
inline INT64 round_to_nearest(FPR f)
{
	//return (INT64)(f.fd + 0.5);
	if (f.fd >= 0)
	{
		return (INT64)(f.fd + 0.5);
	}
	else
	{
		return -(INT64)(-f.fd + 0.5);
	}
}

inline INT64 round_toward_zero(FPR f)
{
	return (INT64)(f.fd);
}

inline INT64 round_toward_positive_infinity(FPR f)
{
	double r = ceil(f.fd);
	return (INT64)(r);
}

inline INT64 round_toward_negative_infinity(FPR f)
{
	double r = floor(f.fd);
	return (INT64)(r);
}
*/

// the following operations/functions CAN BREAK if fast math compiler options (i.e. finite math only) are enabled.
// std::isnan is definetly ignored by GCC/clang then, and there are even fancier compiler heuristics that
// can detect the bit fiddling ops to detect infinity, nan and denorms and then even may ignore these!
// (the latter currently (2024) the case in non-production/experimental clang builds)
inline bool is_nan_double(FPR x)
{
	return std::isnan(x.fd);
}

inline bool is_qnan_double(FPR x)
{
	UINT64 expfrac = (x.id & (DOUBLE_EXP | DOUBLE_FRAC));
	return (expfrac >= (DOUBLE_EXP | 0x0008000000000000ULL));
}

inline bool is_snan_double(FPR x)
{
	UINT64 expfrac = (x.id & (DOUBLE_EXP | DOUBLE_FRAC));
	return ((expfrac > DOUBLE_EXP) && (expfrac < (DOUBLE_EXP | 0x0008000000000000ULL)));
}

inline bool is_infinity_double(FPR x)
{
	return ((x.id & (DOUBLE_EXP | DOUBLE_FRAC)) == DOUBLE_EXP);
}

inline bool is_normalized_double(FPR x)
{
	UINT64 exp = (x.id & DOUBLE_EXP);
	return ((exp > 0) && (exp <= (2046ull << 52)));
}

inline bool is_denormalized_double(FPR x)
{
	UINT64 expfrac = (x.id & (DOUBLE_EXP | DOUBLE_FRAC));
	return ((expfrac > 0) && (expfrac <= DOUBLE_FRAC));
}

inline bool sign_double(FPR x)
{
	return (x.id >> 63);
}

// in theory the following 2 functions require compiler options to work correctly
//  -frounding-math for GCC
//   GCC and clang can show weird behavior nevertheless according to various threads on the net
//  /fp:strict for MS Visual Studio/C++
//   here we use a set of pragmas at the beginning of the file instead (to steer selective behavior for just this file)
// 
// unknown if any games actually change this, at least its extremely rare for Model3

// we just 'cache' FE_TONEAREST, as other math functions that are called
// by other code inbetween PPC emulation could be influenced in a bad way,
// as these may expect FE_TONEAREST to be set
inline void init_rounding_mode(void)
{
	static constexpr unsigned int rounding_mode[4] = {FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};

	if ((ppc.fpscr & 3) != 0) // rounding mode not nearest?
		fesetround(rounding_mode[ppc.fpscr & 3]);
}

inline void restore_rounding_mode(void)
{
	if ((ppc.fpscr & 3) != 0) // rounding mode not nearest?
		fesetround(FE_TONEAREST);
}

/*
inline INT64 smround_to_nearest(FPR f)
{
	// This method is incorrect; it ties away from zero, while PowerPC ties to even
	if (f.fd >= 0)
	{
		return (INT64)(f.fd + 0.5);
	}
	else
	{
		return -(INT64)(-f.fd + 0.5);
	}
}

inline INT64 smround_toward_zero(FPR f)
{
	return (INT64)(f.fd);
}

inline INT64 round_toward_positive_infinity(FPR f)
{
	double r = ceil(f.fd);
	return (INT64)(r);
}

inline INT64 round_toward_negative_infinity(FPR f)
{
	double r = floor(f.fd);
	return (INT64)(r);
}
*/

// TODO (verified via spec, not from MAME): these set only FX, not VXSNAN (0x01000000) itself or the VX summary bit
#define SET_VXSNAN(a, b)    if (is_snan_double(a) || is_snan_double(b)) ppc.fpscr |= 0x80000000
#define SET_VXSNAN_1(c)     if (is_snan_double(c)) ppc.fpscr |= 0x80000000

inline void set_fprf(FPR f)
{
	UINT32 fprf;

	// see page 3-30, 3-31

	if (is_qnan_double(f))
	{
		fprf = 0x11;
	}
	else if (is_infinity_double(f))
	{
		if (sign_double(f))		// -INF
			fprf = 0x09;
		else					// +INF
			fprf = 0x05;
	}
	else if (is_normalized_double(f))
	{
		if (sign_double(f))		// -Normalized
			fprf = 0x08;
		else					// +Normalized
			fprf = 0x04;
	}
	else if (is_denormalized_double(f))
	{
		if (sign_double(f))		// -Denormalized
			fprf = 0x18;
		else					// +Denormalized
			fprf = 0x14;
	}
	else    // Zero
	{
		if (sign_double(f))		// -Zero
			fprf = 0x12;
		else					// +Zero
			fprf = 0x02;
	}

	ppc.fpscr &= ~0x0001f000;
	ppc.fpscr |= (fprf << 12);
}




static void ppc_lfs(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	if(a)
		ea += REG(a);

	f.i = READ32(ea);
	FPR(t).fd = (double)(f.f);
}

static void ppc_lfsu(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	ea += REG(a);

	f.i = READ32(ea);
	FPR(t).fd = (double)(f.f);

	REG(a) = ea;
}

static void ppc_lfd(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;

	if(a)
		ea += REG(a);

	FPR(t).id = READ64(ea);
}

static void ppc_lfdu(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 d = RD;

	ea += REG(a);

	FPR(d).id = READ64(ea);

	REG(a) = ea;
}

static void ppc_stfs(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	if(a)
		ea += REG(a);

	f.f = (float)(FPR(t).fd);
	WRITE32(ea, f.i);
}

static void ppc_stfsu(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	ea += REG(a);

	f.f = (float)(FPR(t).fd);
	WRITE32(ea, f.i);

	REG(a) = ea;
}

static void ppc_stfd(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;

	if(a)
		ea += REG(a);

	WRITE64(ea, FPR(t).id);
}

static void ppc_stfdu(UINT32 op)
{
	UINT32 ea = SIMM16;
	UINT32 a = RA;
	UINT32 t = RT;

	ea += REG(a);

	WRITE64(ea, FPR(t).id);

	REG(a) = ea;
}

static void ppc_lfdux(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 d = RD;

	ea += REG(a);

	FPR(d).id = READ64(ea);

	REG(a) = ea;
}

static void ppc_lfdx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 d = RD;

	if(a)
		ea += REG(a);

	FPR(d).id = READ64(ea);
}

static void ppc_lfsux(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	ea += REG(a);

	f.i = READ32(ea);
	FPR(t).fd = (double)(f.f);

	REG(a) = ea;
}

static void ppc_lfsx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	if(a)
		ea += REG(a);

	f.i = READ32(ea);
	FPR(t).fd = (double)(f.f);
}

static void ppc_mfsr(UINT32 op)
{
	UINT32 sr = (op >> 16) & 15;
	UINT32 t = RT;

	CHECK_SUPERVISOR();

	REG(t) = ppc.sr[sr];
}

static void ppc_mfsrin(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_SUPERVISOR();

	REG(t) = ppc.sr[REG(b) >> 28];
}

static void ppc_mftb(UINT32 op)
{
	UINT32 x = SPRF;

	switch(x)
	{
		case 268:	REG(RT) = (UINT32)(ppc_read_timebase()); break;
		case 269:	REG(RT) = (UINT32)(ppc_read_timebase() >> 32); break;
		default:	
			ErrorLog("PowerPC read from an invalid register. Halting emulation until reset.");
			DebugLog("ppc: Invalid timebase register %d at %08X\n", x, ppc.pc);
			ppc.fatalError = true;
			break;
	}
}

static void ppc_mtsr(UINT32 op)
{
	UINT32 sr = (op >> 16) & 15;
	UINT32 t = RT;

	CHECK_SUPERVISOR();

	ppc.sr[sr] = REG(t);
}

static void ppc_mtsrin(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_SUPERVISOR();

	ppc.sr[REG(b) >> 28] = REG(t);
}

static void ppc_dcba(UINT32 op)
{
	/* TODO: Cache not emulated so this opcode doesn't need to be implemented */
}

static void ppc_stfdux(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;

	ea += REG(a);

	WRITE64(ea, FPR(t).id);

	REG(a) = ea;
}

static void ppc_stfdx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;

	if(a)
		ea += REG(a);

	WRITE64(ea, FPR(t).id);
}

static void ppc_stfiwx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;

	if(a)
		ea += REG(a);

	WRITE32(ea, (UINT32)FPR(t).id);
}

static void ppc_stfsux(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	ea += REG(a);

	f.f = (float)(FPR(t).fd);
	WRITE32(ea, f.i);

	REG(a) = ea;
}

static void ppc_stfsx(UINT32 op)
{
	UINT32 ea = REG(RB);
	UINT32 a = RA;
	UINT32 t = RT;
	FPR32 f;

	if(a)
		ea += REG(a);

	f.f = (float)(FPR(t).fd);

	WRITE32(ea, f.i);
}

static void ppc_tlbia(UINT32 op)
{
	/* TODO: TLB not emulated so this opcode doesn't need to implemented */
}

static void ppc_tlbie(UINT32 op)
{
	/* TODO: TLB not emulated so this opcode doesn't need to implemented */
}

static void ppc_tlbsync(UINT32 op)
{
	/* TODO: TLB not emulated so this opcode doesn't need to implemented */
}

static void ppc_eciwx(UINT32 op)
{
	ppc_unimplemented(op);
}

static void ppc_ecowx(UINT32 op)
{
	ppc_unimplemented(op);
}

static void ppc_fabsx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	FPR(t).id = FPR(b).id & ~DOUBLE_SIGN;

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_faddx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = FPR(a).fd + FPR(b).fd;

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fcmpo(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = (RT >> 2);
	UINT32 c;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	if(is_nan_double(FPR(a)) || is_nan_double(FPR(b)))
	{
		c = 1; /* OX */
		// from MAME (ppccom_fcmp_vx), without its update of VX and FEX
		UINT32 newexc = 0;
		if(is_snan_double(FPR(a)) || is_snan_double(FPR(b))) {
			newexc |= 0x01000000; /* VXSNAN */
			if(!(ppc.fpscr & 0x00000080)) /* VE */
				newexc |= 0x00080000; /* VXVC */
		}
		else {
			// unordered without an SNaN case, such as QNaN
			newexc |= 0x00080000; /* VXVC */
		}
		if(newexc & ~ppc.fpscr)
			ppc.fpscr |= 0x80000000; /* FX */
		ppc.fpscr |= newexc;
	}
	else if(FPR(a).fd < FPR(b).fd){
		c = 8; /* FX */
	}
	else if(FPR(a).fd > FPR(b).fd){
		c = 4; /* FEX */
	}
	else {
		c = 2; /* VX */
	}

	CR(t) = c;

	// TODO: like MAME, set only FPCC (0x0000F000) and leave the C bit (0x00010000) alone
	// Enabled by Bart
	ppc.fpscr &= ~0x0001F000;
	ppc.fpscr |= (c << 12);
}

static void ppc_fcmpu(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = (RT >> 2);
	UINT32 c;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	if(is_nan_double(FPR(a)) || is_nan_double(FPR(b)))
	{
		c = 1; /* OX */
		if(is_snan_double(FPR(a)) || is_snan_double(FPR(b))) {
			ppc.fpscr |= 0x01000000; /* VXSNAN */
		}
	}
	else if(FPR(a).fd < FPR(b).fd){
		c = 8; /* FX */
	}
	else if(FPR(a).fd > FPR(b).fd){
		c = 4; /* FEX */
	}
	else {
		c = 2; /* VX */
	}

	CR(t) = c;

	// TODO: like MAME, set only FPCC (0x0000F000) and leave the C bit (0x00010000) alone
	ppc.fpscr &= ~0x0001F000;
	ppc.fpscr |= (c << 12);
}

static void ppc_fctiwx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;
	double r = 0.0;

	// TODO: fix FPSCR flags FX,VXSNAN,VXCVI

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	switch(ppc.fpscr & 3)
	{
		case 0: init_rounding_mode(); r = nearbyint(FPR(b).fd); restore_rounding_mode(); break;
		case 1: r = trunc(FPR(b).fd); break;
		case 2: r = ceil(FPR(b).fd); break;
		case 3: r = floor(FPR(b).fd); break;
	}

	// verified via spec, not from MAME: clamp as double, as converting NaN or out-of-range values to an integer is undefined (x86 and ARM differ).
	// NaN is tested on the bits, as fast math may drop std::isnan
	if(r < -2147483648.0 || (FPR(b).id & (DOUBLE_EXP | DOUBLE_FRAC)) > DOUBLE_EXP)	// or NaN
	{
		FPR(t).id = (INT64)((INT32)0x80000000);
		// FPSCR[FR] = 1
		// FPSCR[FI] = 1
		// FPSCR[XX] = 1
	}
	else if(r > 2147483647.0)
	{
		FPR(t).id = (INT64)((INT32)0x7FFFFFFF);
		// FPSCR[FR] = 0
		// FPSCR[FI] = 1
		// FPSCR[XX] = 1
	}
	else
	{
		FPR(t).id = (UINT32)(INT32)r;
		// FPSCR[FR] = t.iw > t.fd
		// FPSCR[FI] = t.iw == t.fd
		// FPSCR[XX] = ?
	}

	// FPSCR[FPRF] = undefined (leave it as is)
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fctiwzx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;
	double r;

	// TODO: fix FPSCR flags FX,VXSNAN,VXCVI

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));
	r = trunc(FPR(b).fd);

	// verified via spec, not from MAME: clamp as double, as converting NaN or out-of-range values to an integer is undefined (x86 and ARM differ).
	// NaN is tested on the bits, as fast math may drop std::isnan
	if(r < -2147483648.0 || (FPR(b).id & (DOUBLE_EXP | DOUBLE_FRAC)) > DOUBLE_EXP)	// or NaN
	{
		FPR(t).id = (INT64)((INT32)0x80000000);
		// FPSCR[FR] = 1
		// FPSCR[FI] = 1
		// FPSCR[XX] = 1
	}
	else if(r > 2147483647.0)
	{
		FPR(t).id = (INT64)((INT32)0x7FFFFFFF);
		// FPSCR[FR] = 0
		// FPSCR[FI] = 1
		// FPSCR[XX] = 1

	}
	else
	{
		FPR(t).id = (UINT32)(INT32)r;
		// FPSCR[FR] = t.iw > t.fd
		// FPSCR[FI] = t.iw == t.fd
		// FPSCR[XX] = ?
	}

	// FPSCR[FPRF] = undefined (leave it as is)
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fdivx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = FPR(a).fd / FPR(b).fd;

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmrx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	FPR(t).fd = FPR(b).fd;

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnabsx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	FPR(t).id = FPR(b).id | DOUBLE_SIGN;

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnegx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	FPR(t).id = FPR(b).id ^ DOUBLE_SIGN;

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_frspx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	FPR(t).fd = (float)FPR(b).fd;	// TODO (verified via spec, not from MAME): ignores FPSCR[RN] (always rounds to nearest)

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_frsqrtex(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	init_rounding_mode();

	FPR(t).fd = 1.0 / sqrt(FPR(b).fd);  /* verify this */

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fsqrtx(UINT32 op)
{
	/* NOTE: PPC603e doesn't support this opcode */
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	init_rounding_mode();

	FPR(t).fd = sqrt(FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fsubx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = FPR(a).fd - FPR(b).fd;

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mffsx(UINT32 op)
{
	FPR(RT).id = (UINT32)ppc.fpscr;

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mtfsb0x(UINT32 op)
{
	UINT32 crbD;

	crbD = (op >> 21) & 0x1F;

	if (crbD != 1 && crbD != 2) // these bits cannot be explicitly cleared
		ppc.fpscr &= ~(1 << (31 - crbD));

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mtfsb1x(UINT32 op)
{
	UINT32 crbD;

	crbD = (op >> 21) & 0x1F;

	if (crbD != 1 && crbD != 2) // these bits cannot be explicitly cleared
		ppc.fpscr |= (1 << (31 - crbD));

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mtfsfx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 f = ppc_field_xlat[FM] & ~(FPSCR_FEX | FPSCR_VX);	// verified via spec, not from MAME (which sets them): FEX and VX are not set from frB

	// verified via spec, not from MAME: replace only the selected fields (was: OR in all of frB, whatever FM)
	ppc.fpscr = (ppc.fpscr & ~f) | ((UINT32)(FPR(b).id) & f);
	// TODO (verified via spec, not from MAME): FEX and VX should be recomputed from the new exception and enable bits (also in mtfsfi)

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mtfsfix(UINT32 op)
{
    UINT32 crfd = CRFD;
    UINT32 imm = (op >> 12) & 0xF;

    /*
     * According to the manual:
     *
     * If bits 0 and 3 of FPSCR are to be modified, they take the immediate
     * value specified. Bits 1 and 2 (FEX and VX) are set according to the
     * "usual rule" and not from IMM[1-2].
     *
     * The "usual rule" is not emulated, so these bits simply aren't modified
     * at all here.
     */

    crfd = (7 - crfd) * 4;  // calculate LSB position of field

    // verified via spec, not from MAME: write only the selected field (for field 0, imm was also ORed into the RN bits, and FEX and VX set from it)
    UINT32 mask = 0xf << crfd;
    if (crfd == 28)         // field containing FEX and VX is special...
        mask &= ~(FPSCR_FEX | FPSCR_VX);    // bits 1 and 2 of FPSCR must not be altered

    ppc.fpscr &= ~mask;                 // clear field
    ppc.fpscr |= (imm << crfd) & mask;  // insert new data

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_mcrfs(UINT32 op)
{
	UINT32 crfs, f;
	crfs = CRFA;

	f = ppc.fpscr >> ((7 - crfs) * 4);	// get crfS field from FPSCR
	f &= 0xf;

	switch(crfs)	// determine which exception bits to clear in FPSCR
	{
		case 0:		// FX, OX
			ppc.fpscr &= ~0x90000000;
			break;
		case 1:		// UX, ZX, XX, VXSNAN
			ppc.fpscr &= ~0x0f000000;
			break;
		case 2:		// VXISI, VXIDI, VXZDZ, VXIMZ
			ppc.fpscr &= ~0x00F00000;
			break;
		case 3:		// VXVC
			ppc.fpscr &= ~0x00080000;
			break;
		case 5:		// VXSOFT, VXSQRT, VXCVI
			ppc.fpscr &= ~0x00000700;
			break;
		default:
			break;
	}

	CR(CRFD) = f;
}

static void ppc_faddsx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = (float)(FPR(a).fd + FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fdivsx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = (float)(FPR(a).fd / FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fresx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	init_rounding_mode();

	// On the 603 fres behaves the same as fdivs RT, 1.0, RB
	FPR(t).fd = (float)(1.0 / FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fsqrtsx(UINT32 op)
{
	/* NOTE: This opcode is not supported in PPC603e */
	UINT32 b = RB;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN_1(FPR(b));

	init_rounding_mode();

	FPR(t).fd = (float)(sqrt(FPR(b).fd));

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fsubsx(UINT32 op)
{
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));

	init_rounding_mode();

	FPR(t).fd = (float)(FPR(a).fd - FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmaddx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = ((FPR(a).fd * FPR(c).fd) + FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmsubx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = ((FPR(a).fd * FPR(c).fd) - FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmulx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(c));

	init_rounding_mode();

	FPR(t).fd = (FPR(a).fd * FPR(c).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnmaddx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (-((FPR(a).fd * FPR(c).fd) + FPR(b).fd));

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnmsubx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (-((FPR(a).fd * FPR(c).fd) - FPR(b).fd));

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fselx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	FPR(t).fd = (is_nan_double(FPR(a)) || FPR(a).fd < 0.0) ? FPR(b).fd : FPR(c).fd; // do not just sign check, as -0.0 also counts as +0.0

	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmaddsx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (float)((FPR(a).fd * FPR(c).fd) + FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmsubsx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (float)((FPR(a).fd * FPR(c).fd) - FPR(b).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fmulsx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();
	SET_VXSNAN(FPR(a), FPR(c));

	init_rounding_mode();

	FPR(t).fd = (float)(FPR(a).fd * FPR(c).fd);

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnmaddsx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (float)(-((FPR(a).fd * FPR(c).fd) + FPR(b).fd));

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}

static void ppc_fnmsubsx(UINT32 op)
{
	UINT32 c = RC;
	UINT32 b = RB;
	UINT32 a = RA;
	UINT32 t = RT;

	CHECK_FPU_AVAILABLE();

	SET_VXSNAN(FPR(a), FPR(b));
	SET_VXSNAN_1(FPR(c));

	init_rounding_mode();

	FPR(t).fd = (float)(-((FPR(a).fd * FPR(c).fd) - FPR(b).fd));

	restore_rounding_mode();

	set_fprf(FPR(t));
	if( RCBIT ) {
		SET_CR1();
	}
}
