/* radare2 - LGPL - Copyright 2014-2019 - pancake */

#include <r_anal.h>
#include <r_lib.h>
#include <capstone.h>
#include <systemz.h>
// instruction set: http://www.tachyonsoft.com/inst390m.htm

#if CS_API_MAJOR < 2
#error Old Capstone not supported
#endif

#define esilprintf(op, fmt, ...) r_strbuf_setf (&op->esil, fmt, ##__VA_ARGS__)
#define INSOP(n) insn->detail->sysz.operands[n]

static void opex(RStrBuf *buf, csh handle, cs_insn *insn) {
	int i;
	PJ *pj;
	pj = pj_new ();
	if (!pj) {
		return;
	}
	pj_o (pj);
	cs_sysz *x = &insn->detail->sysz;
	pj_ka (pj, "operands");
	for (i = 0; i < x->op_count; i++) {
		cs_sysz_op *op = &x->operands[i];
		pj_o (pj);
		switch (op->type) {
		case SYSZ_OP_REG:
			pj_ks (pj, "type", "reg");
			pj_ks (pj, "value", cs_reg_name (handle, op->reg));
			break;
		case SYSZ_OP_IMM:
			pj_ks (pj, "type", "imm");
			pj_kS (pj, "value", (st64)op->imm);
			break;
		case SYSZ_OP_MEM:
			pj_ks (pj, "type", "mem");
			if (op->mem.base != SYSZ_REG_INVALID) {
				pj_ks (pj, "base", cs_reg_name (handle, op->mem.base));
			}
			pj_kS (pj, "disp", (st64)op->mem.disp);
			break;
		default:
			pj_ks (pj, "type", "invalid");
			break;
		}
		pj_o (pj); /* o operand */
	}
	pj_end (pj); /* a operands */
	pj_end (pj);

	char *s = pj_drain (pj);
	r_strbuf_init (buf);
	r_strbuf_append (buf, s);

	free (s);
}

static int analop(RAnal *a, RAnalOp *op, ut64 addr, const ut8 *buf, int len, RAnalOpMask mask) {
	csh handle;
	cs_insn *insn;
	int mode = CS_MODE_BIG_ENDIAN;
	int ret = cs_open (CS_ARCH_SYSZ, mode, &handle);
	if (ret == CS_ERR_OK) {
		cs_option (handle, CS_OPT_DETAIL, CS_OPT_ON);
		// capstone-next
		int n = cs_disasm (handle, (const ut8*)buf, len, addr, 1, &insn);
		if (n < 1) {
			op->type = R_ANAL_OP_TYPE_ILL;
		} else {
			if (mask & R_ANAL_OP_MASK_OPEX) {
				opex (&op->opex, handle, insn);
			}
			op->size = insn->size;
			switch (insn->id) {
			case SYSZ_INS_BRCL:
			case SYSZ_INS_BRASL:
				op->type = R_ANAL_OP_TYPE_CALL;
				break;
			case SYSZ_INS_BR:
				op->type = R_ANAL_OP_TYPE_JMP;
				break;
			case SYSZ_INS_BRC:
			case SYSZ_INS_BER:
			case SYSZ_INS_BHR:
			case SYSZ_INS_BHER:
			case SYSZ_INS_BLR:
			case SYSZ_INS_BLER:
			case SYSZ_INS_BLHR:
			case SYSZ_INS_BNER:
			case SYSZ_INS_BNHR:
			case SYSZ_INS_BNHER:
			case SYSZ_INS_BNLR:
			case SYSZ_INS_BNLER:
			case SYSZ_INS_BNLHR:
			case SYSZ_INS_BNOR:
			case SYSZ_INS_BOR:
			case SYSZ_INS_BASR:
			case SYSZ_INS_BRAS:
			case SYSZ_INS_BRCT:
			case SYSZ_INS_BRCTG:
				op->type = R_ANAL_OP_TYPE_CJMP;
				break;
			case SYSZ_INS_JE:
			case SYSZ_INS_JGE:
			case SYSZ_INS_JHE:
			case SYSZ_INS_JGHE:
			case SYSZ_INS_JH:
			case SYSZ_INS_JGH:
			case SYSZ_INS_JLE:
			case SYSZ_INS_JGLE:
			case SYSZ_INS_JLH:
			case SYSZ_INS_JGLH:
			case SYSZ_INS_JL:
			case SYSZ_INS_JGL:
			case SYSZ_INS_JNE:
			case SYSZ_INS_JGNE:
			case SYSZ_INS_JNHE:
			case SYSZ_INS_JGNHE:
			case SYSZ_INS_JNH:
			case SYSZ_INS_JGNH:
			case SYSZ_INS_JNLE:
			case SYSZ_INS_JGNLE:
			case SYSZ_INS_JNLH:
			case SYSZ_INS_JGNLH:
			case SYSZ_INS_JNL:
			case SYSZ_INS_JGNL:
			case SYSZ_INS_JNO:
			case SYSZ_INS_JGNO:
			case SYSZ_INS_JO:
			case SYSZ_INS_JGO:
			case SYSZ_INS_JG:
				op->type = R_ANAL_OP_TYPE_CJMP;
				op->jump = INSOP(0).imm;
				op->fail = addr+op->size;
				break;
			case SYSZ_INS_J:
				op->type = R_ANAL_OP_TYPE_JMP;
				op->jump = INSOP(0).imm;
				op->fail = UT64_MAX;
				break;
			}
		}
		cs_free (insn, n);
		cs_close (&handle);
	}
	return op->size;
}

static bool set_reg_profile(RAnal *anal) {
	const char *p =
		"=PC	r15\n"
		"=LR	r14\n"
		"=SP	r13\n"
		"=BP	r12\n"
		"=A0	r0\n"
		"=A1	r1\n"
		"=A2	r2\n"
		"=A3	r3\n"
		"=SN	r0\n"
		"gpr	sb	.32	36	0\n" // r9
		"gpr	sl	.32	40	0\n" // rl0
		"gpr	fp	.32	44	0\n" // r11
		"gpr	ip	.32	48	0\n" // r12
		"gpr	sp	.32	52	0\n" // r13
		"gpr	lr	.32	56	0\n" // r14
		"gpr	pc	.32	60	0\n" // r15

		"gpr	r0	.32	0	0\n"
		"gpr	r1	.32	4	0\n"
		"gpr	r2	.32	8	0\n"
		"gpr	r3	.32	12	0\n"
		"gpr	r4	.32	16	0\n"
		"gpr	r5	.32	20	0\n"
		"gpr	r6	.32	24	0\n"
		"gpr	r7	.32	28	0\n"
		"gpr	r8	.32	32	0\n"
		"gpr	r9	.32	36	0\n"
		"gpr	r10	.32	40	0\n"
		"gpr	r11	.32	44	0\n"
		"gpr	r12	.32	48	0\n"
		"gpr	r13	.32	52	0\n"
		"gpr	r14	.32	56	0\n"
		"gpr	r15	.32	60	0\n"
	;
	return r_reg_set_profile_string (anal->reg, p);
}

static int archinfo(RAnal *anal, int q) {
	switch (q) {
	case R_ANAL_ARCHINFO_ALIGN:
		return 2;
	case R_ANAL_ARCHINFO_MAX_OP_SIZE:
		return 4;
	case R_ANAL_ARCHINFO_MIN_OP_SIZE:
		return 2;
	}
	return 2;
}

RAnalPlugin r_anal_plugin_sysz = {
	.name = "sysz",
	.desc = "Capstone SystemZ microanalysis",
	.esil = false,
	.license = "BSD",
	.arch = "sysz",
	.bits = 32|64,
	.op = &analop,
	.archinfo = archinfo,
	.set_reg_profile = &set_reg_profile,
};

#ifndef R2_PLUGIN_INCORE
R_API RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_ANAL,
	.data = &r_anal_plugin_sysz,
	.version = R2_VERSION
};
#endif
