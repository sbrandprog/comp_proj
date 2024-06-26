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

#define COMPILE_LO_WORKER_TIMEOUT 1000
#define COMPILE_LO_WORKER_COUNT 4

typedef struct ira_pec_c_ctx_cl_elem {
	ira_lo_t * lo;
} cl_elem_t;

typedef struct ira_pec_c_ctx {
	ira_pec_t * pec;
	asm_pea_t * out;

	ul_hsb_t hsb;
	ul_hst_t * hst;

	size_t str_unq_index;

	size_t cl_elems_size;
	cl_elem_t * cl_elems;
	size_t cl_elems_cap;

	PTP_WORK cl_work;
	CRITICAL_SECTION cl_lock;
	CONDITION_VARIABLE cl_cv;
	size_t cl_elems_cur;
	size_t cl_wips;
	bool cl_err_flag;

	CRITICAL_SECTION cl_out_it_lock;
	CRITICAL_SECTION cl_frag_lock;
	asm_frag_t * cl_frag;
	asm_frag_t ** cl_frag_ins;
} ctx_t;

static asm_frag_t * get_frag_nl(ctx_t * ctx, asm_frag_type_t frag_type) {
	*ctx->cl_frag_ins = asm_frag_create(frag_type);

	asm_frag_t * frag = *ctx->cl_frag_ins;

	ctx->cl_frag_ins = &frag->next;

	return frag;
}
asm_frag_t * ira_pec_c_get_frag(ira_pec_c_ctx_t * ctx, asm_frag_type_t frag_type) {
	EnterCriticalSection(&ctx->cl_frag_lock);

	asm_frag_t * res = get_frag_nl(ctx, frag_type);

	LeaveCriticalSection(&ctx->cl_frag_lock);

	return res;
}

static ul_hs_t * get_unq_arr_label(ctx_t * ctx) {
	return ul_hsb_formatadd(&ctx->hsb, ctx->hst, L"%s%zi", ARR_UNQ_PREFIX, ctx->str_unq_index++);
}

static bool is_lo_compilable(ira_lo_t * lo) {
	switch (lo->type) {
		case IraLoNone:
		case IraLoNspc:
			return false;
		case IraLoFunc:
		case IraLoImpt:
		case IraLoVar:
			break;
		case IraLoDtStct:
		case IraLoRoVal:
			return false;
		default:
			ul_assert_unreachable();
	}

	return true;
}
bool ira_pec_c_process_val_compl(ira_pec_c_ctx_t * ctx, ira_val_t * val) {
	switch (val->type) {
		case IraValImmVoid:
			break;
		case IraValImmDt:
			return false;
		case IraValImmBool:
		case IraValImmInt:
		case IraValImmPtr:
			break;
		case IraValLoPtr:
			if (!is_lo_compilable(val->lo_val)) {
				return false;
			}

			ira_pec_c_push_cl_elem(ctx, val->lo_val);
			break;
		case IraValImmStct:
		{
			ira_dt_sd_t * sd = val->dt->stct.lo->dt_stct.sd;

			if (sd == NULL) {
				return false;
			}

			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + sd->elems_size; elem != elem_end; ++elem) {
				if (!ira_pec_c_process_val_compl(ctx, *elem)) {
					return false;
				}
			}
			break;
		}
		case IraValImmArr:
			for (ira_val_t ** elem = val->arr_val.data, **elem_end = elem + val->arr_val.size; elem != elem_end; ++elem) {
				if (!ira_pec_c_process_val_compl(ctx, *elem)) {
					return false;
				}
			}
			break;
		default:
			ul_assert_unreachable();
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
			if (!is_lo_compilable(val->lo_val)) {
				return false;
			}

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
			ul_assert_unreachable();
	}


	return true;
}
bool ira_pec_c_compile_val_frag(ira_pec_c_ctx_t * ctx, ira_val_t * val, ul_hs_t ** out_label) {
	asm_frag_t * frag = ira_pec_c_get_frag(ctx, AsmFragRoData);

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

static void push_cl_elem_nl(ctx_t * ctx, ira_lo_t * lo) {
	for (cl_elem_t * elem = ctx->cl_elems, *elem_end = elem + ctx->cl_elems_size; elem != elem_end; ++elem) {
		if (elem->lo == lo) {
			return;
		}
	}
	
	if (ctx->cl_elems_size + 1 >= ctx->cl_elems_cap) {
		ul_arr_grow(&ctx->cl_elems_cap, &ctx->cl_elems, sizeof(*ctx->cl_elems), 1);
	}

	ctx->cl_elems[ctx->cl_elems_size++] = (cl_elem_t){ .lo = lo };
}
void ira_pec_c_push_cl_elem(ira_pec_c_ctx_t * ctx, ira_lo_t * lo) {
	EnterCriticalSection(&ctx->cl_lock);

	push_cl_elem_nl(ctx, lo);

	LeaveCriticalSection(&ctx->cl_lock);

	WakeConditionVariable(&ctx->cl_cv);
}

static bool compile_lo_impt(ctx_t * ctx, ira_lo_t * lo) {
	EnterCriticalSection(&ctx->cl_out_it_lock);

	asm_it_add_sym(&ctx->out->it, lo->impt.lib_name, lo->impt.sym_name, lo->full_name);

	LeaveCriticalSection(&ctx->cl_out_it_lock);

	return true;
}
static bool compile_lo_var(ctx_t * ctx, ira_lo_t * lo) {
	if (!ira_dt_is_complete(lo->var.qdt.dt)) {
		return false;
	}

	asm_frag_type_t frag_type = lo->var.qdt.qual.const_q ? AsmFragRoData : AsmFragWrData;

	asm_frag_t * frag = ira_pec_c_get_frag(ctx, frag_type);

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
		case IraLoFunc:
			if (!ira_pec_ip_compile(ctx, lo)) {
				return false;
			}
			break;
		case IraLoImpt:
			if (!compile_lo_impt(ctx, lo)) {
				return false;
			}
			break;
		case IraLoVar:
			if (!compile_lo_var(ctx, lo)) {
				return false;
			}
			break;
		default:
			ul_assert_unreachable();
	}

	return true;
}

static bool get_cl_elem(ctx_t * ctx, cl_elem_t * out) {
	EnterCriticalSection(&ctx->cl_lock);

	bool res = false;

	while (true) {
		if (ctx->cl_elems_cur < ctx->cl_elems_size) {
			*out = ctx->cl_elems[ctx->cl_elems_cur++];

			InterlockedIncrement64(&ctx->cl_wips);

			res = true;

			break;
		}
		else if (ctx->cl_wips == 0) {
			break;
		}

		SleepConditionVariableCS(&ctx->cl_cv, &ctx->cl_lock, COMPILE_LO_WORKER_TIMEOUT);
	}

	LeaveCriticalSection(&ctx->cl_lock);

	return res;
}
static bool process_cl_elem(ctx_t * ctx, cl_elem_t * elem) {
	bool res = true;

	if (!compile_lo(ctx, elem->lo)) {
		res = false;
	}

	if (InterlockedDecrement64(&ctx->cl_wips) == 0) {
		WakeAllConditionVariable(&ctx->cl_cv);
	}

	return res;
}
static VOID compile_lo_worker(PTP_CALLBACK_INSTANCE itnc, PVOID user_data, PTP_WORK work) {
	CallbackMayRunLong(itnc);

	ctx_t * ctx = user_data;
	cl_elem_t elem;

	while (get_cl_elem(ctx, &elem)) {
		if (!process_cl_elem(ctx, &elem)) {
			ctx->cl_err_flag = true;
		}
	}
}

static ira_lo_t * find_lo(ira_lo_t * nspc, ul_hs_t * name) {
	for (ira_lo_t * lo = nspc->nspc.body; lo != NULL; lo = lo->next) {
		if (lo->name == name) {
			return lo;
		}
	}

	return NULL;
}

static bool prepare_data(ctx_t * ctx) {
	ira_lo_t * ep_lo = find_lo(ctx->pec->root, ctx->pec->ep_name);

	if (ep_lo == NULL) {
		return false;
	}

	push_cl_elem_nl(ctx, ep_lo);

	ctx->hst = ctx->pec->hst;

	ul_hsb_init(&ctx->hsb);

	asm_pea_init(ctx->out, ctx->hst);

	ctx->out->ep_name = ctx->pec->ep_name;

	InitializeCriticalSection(&ctx->cl_lock);

	InitializeConditionVariable(&ctx->cl_cv);

	InitializeCriticalSection(&ctx->cl_out_it_lock);

	InitializeCriticalSection(&ctx->cl_frag_lock);

	ctx->cl_frag_ins = &ctx->cl_frag;

	return true;
}
static bool do_work(ctx_t * ctx) {
	ctx->cl_work = CreateThreadpoolWork(compile_lo_worker, ctx, NULL);

	if (ctx->cl_work == NULL) {
		return false;
	}

	for (size_t i = 0; i < COMPILE_LO_WORKER_COUNT; ++i) {
		SubmitThreadpoolWork(ctx->cl_work);
	}

	WaitForThreadpoolWorkCallbacks(ctx->cl_work, FALSE);

	CloseThreadpoolWork(ctx->cl_work);

	ctx->cl_work = NULL;

	return !ctx->cl_err_flag;
}
static void move_frags(ctx_t * ctx) {
	ctx->out->frag = ctx->cl_frag;

	ctx->cl_frag = NULL;
}
static bool compile_core(ctx_t * ctx) {
	if (!prepare_data(ctx)) {
		return false;
	}

	if (!do_work(ctx)) {
		return false;
	}

	move_frags(ctx);

	return true;
}
bool ira_pec_c_compile(ira_pec_t * pec, asm_pea_t * out) {
	ctx_t ctx = { .pec = pec, .out = out };

	bool res = compile_core(&ctx);

	if (ctx.cl_work != NULL) {
		CloseThreadpoolWork(ctx.cl_work);
	}

	DeleteCriticalSection(&ctx.cl_frag_lock);

	asm_frag_destroy_chain(ctx.cl_frag);

	DeleteCriticalSection(&ctx.cl_out_it_lock);

	DeleteCriticalSection(&ctx.cl_lock);

	free(ctx.cl_elems);

	ul_hsb_cleanup(&ctx.hsb);

	return res;
}
