#include "pch.h"
#include "ira_pec_c.h"
#include "ira_val.h"
#include "ira_lo.h"
#include "ira_pec_ip.h"
#include "asm_inst.h"
#include "asm_frag.h"
#include "asm_pea.h"

#define LO_NAME_DELIM L'.'

#define ARR_UNQ_PREFIX L"#arr"

typedef struct ira_pec_c_ctx {
	ira_pec_t * pec;

	asm_pea_t * out;

	ul_hsb_t hsb;
	ul_hst_t * hst;

	size_t str_unq_index;
} ctx_t;

static ul_hs_t * get_unq_arr_label(ctx_t * ctx) {
	return ul_hsb_formatadd(&ctx->hsb, ctx->hst, L"%s%zi", ARR_UNQ_PREFIX, ctx->str_unq_index++);
}

bool ira_pec_c_is_val_compilable(ira_val_t * val) {
	switch (val->type) {
		case IraValImmVoid:
			break;
		case IraValImmDt:
			return false;
		case IraValImmBool:
		case IraValImmInt:
		case IraValImmPtr:
		case IraValLoPtr:
			break;
		case IraValImmStct:
		{
			ira_dt_sd_t * sd = val->dt->stct.lo->dt_stct.sd;

			if (sd == NULL) {
				return false;
			}

			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + sd->elems_size; elem != elem_end; ++elem) {
				if (!ira_pec_c_is_val_compilable(*elem)) {
					return false;
				}
			}
			break;
		}
		case IraValImmArr:
			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + val->arr_val.size; elem != elem_end; ++elem) {
				if (!ira_pec_c_is_val_compilable(*elem)) {
					return false;
				}
			}
			break;
		default:
			ul_raise_unreachable();
	}

	return true;
}
static bool compile_val(ctx_t * ctx, asm_frag_t * frag, ira_val_t * val) {
	{
		size_t dt_align;
		
		if (!ira_dt_get_align(val->dt, &dt_align)) {
			return false;
		}

		asm_inst_t align = { .type = AsmInstAlign, .opds = AsmInstOpds_Imm, .imm0_type = AsmInstImm64, .imm0 = (int64_t)dt_align };

		asm_frag_push_inst(frag, &align);
	}

	asm_inst_t data = { .type = AsmInstData, .opds = AsmInstOpds_Imm };

	switch (val->type) {
		case IraValImmVoid:
			break;
		case IraValImmDt:
			return false;
		case IraValImmBool:
			data.imm0_type = AsmInstImm8;
			data.imm0 = val->bool_val ? 1 : 0;
			
			asm_frag_push_inst(frag, &data);
			break;
		case IraValImmInt:
			data.imm0_type = ira_int_infos[val->dt->int_type].asm_imm_type;
			data.imm0 = val->int_val.si64;
			
			asm_frag_push_inst(frag, &data);
			break;
		case IraValImmVec:
			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + val->dt->vec.size; elem != elem_end; ++elem) {
				if (!compile_val(ctx, frag, *elem)) {
					return false;
				}
			}
			break;
		case IraValImmPtr:
			data.imm0_type = AsmInstImm64;
			data.imm0 = (int64_t)val->int_val.ui64;
			
			asm_frag_push_inst(frag, &data);
			break;
		case IraValLoPtr:
			data.imm0_type = AsmInstImmLabelVa64;
			data.imm0_label = val->lo_val->full_name;
			
			asm_frag_push_inst(frag, &data);
			break;
		case IraValImmStct:
		{
			ira_dt_sd_t * sd = val->dt->stct.lo->dt_stct.sd;

			if (sd == NULL) {
				return false;
			}

			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + sd->elems_size; elem != elem_end; ++elem) {
				if (!compile_val(ctx, frag, *elem)) {
					return false;
				}
			}
			break;
		}
		case IraValImmArr:
			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + val->arr_val.size; elem != elem_end; ++elem) {
				if (!compile_val(ctx, frag, *elem)) {
					return false;
				}
			}
			break;
		default:
			ul_raise_unreachable();
	}


	return true;
}
bool ira_pec_c_compile_val_frag(ira_pec_c_ctx_t * ctx, ira_val_t * val, ul_hs_t ** out_label) {
	asm_frag_t * frag = asm_frag_create(AsmFragRoData, &ctx->out->frag);

	ul_hs_t * frag_label = get_unq_arr_label(ctx);

	{
		asm_inst_t label = { .type = AsmInstLabel, .opds = AsmInstLabel, .label = frag_label };

		asm_frag_push_inst(frag, &label);
	}

	if (!compile_val(ctx, frag, val)) {
		return false;
	}

	*out_label = frag_label;

	return true;
}

ul_hsb_t * ira_pec_c_get_hsb(ira_pec_c_ctx_t * ctx) {
	return &ctx->hsb;
}
ul_hst_t * ira_pec_c_get_hst(ira_pec_c_ctx_t * ctx) {
	return ctx->hst;
}
ira_pec_t * ira_pec_c_get_pec(ira_pec_c_ctx_t * ctx) {
	return ctx->pec;
}
asm_pea_t * ira_pec_c_get_pea(ira_pec_c_ctx_t * ctx) {
	return ctx->out;
}

static bool compile_lo_var(ctx_t * ctx, ira_lo_t * lo) {
	if (!ira_dt_is_complete(lo->var.qdt.dt)) {
		return false;
	}

	asm_frag_type_t frag_type = lo->var.qdt.qual.const_q ? AsmFragRoData : AsmFragWrData;

	asm_frag_t * frag = asm_frag_create(frag_type, &ctx->out->frag);

	{
		asm_inst_t label = { .type = AsmInstLabel, .opds = AsmInstOpds_Label, .label = lo->full_name };

		asm_frag_push_inst(frag, &label);
	}

	switch (lo->var.val->type) {
		case IraValImmDt:
			return false;
		case IraValImmVoid:
		case IraValImmBool:
		case IraValImmInt:
		case IraValImmPtr:
		case IraValLoPtr:
		case IraValImmStct:
			if (!compile_val(ctx, frag, lo->var.val)) {
				return false;
			}
			break;
		case IraValImmArr:
		{
			ul_hs_t * arr_label;

			if (!ira_pec_c_compile_val_frag(ctx, lo->var.val, &arr_label)) {
				return false;
			}

			{
				asm_inst_t data = { .type = AsmInstData, .opds = AsmInstOpds_Imm, .imm0_type = AsmInstImm64, .imm0 = (int64_t)lo->var.val->arr_val.size };

				asm_frag_push_inst(frag, &data);
			}

			{
				asm_inst_t data = { .type = AsmInstData, .type = AsmInstData, .opds = AsmInstOpds_Imm, .imm0_type = AsmInstImmLabelVa64, .imm0_label = arr_label };

				asm_frag_push_inst(frag, &data);
			}

			break;
		}
	}

	return true;
}
static bool compile_lo(ctx_t * ctx, ira_lo_t * lo) {
	switch (lo->type) {
		case IraLoNone:
			break;
		case IraLoNspc:
			for (ira_lo_t * it = lo->nspc.body; it != NULL; it = it->next) {
				if (!compile_lo(ctx, it)) {
					return false;
				}
			}
			break;
		case IraLoFunc:
			if (!ira_pec_ip_compile(ctx, lo)) {
				return false;
			}
			break;
		case IraLoImpt:
			asm_it_add_sym(&ctx->out->it, lo->impt.lib_name, lo->impt.sym_name, lo->full_name);
			break;
		case IraLoVar:
			if (!compile_lo_var(ctx, lo)) {
				return false;
			}
			break;
		case IraLoDtStct:
		case IraLoRoVal:
			break;
		default:
			ul_raise_unreachable();
	}

	return true;
}

static bool compile_core(ctx_t * ctx) {
	ctx->hst = ctx->pec->hst;

	ul_hsb_init(&ctx->hsb);

	asm_pea_init(ctx->out, ctx->hst);

	if (!compile_lo(ctx, ctx->pec->root)) {
		return false;
	}

	ctx->out->ep_name = ctx->pec->ep_name;

	return true;
}
bool ira_pec_c_compile(ira_pec_t * pec, asm_pea_t * out) {
	ctx_t ctx = { .pec = pec, .out = out };

	bool res;

	__try {
		res = compile_core(&ctx);
	}
	__finally {
		ul_hsb_cleanup(&ctx.hsb);
	}

	return res;
}