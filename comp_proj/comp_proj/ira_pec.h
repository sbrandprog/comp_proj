#pragma once
#include "ira_pds.h"
#include "ira_dt.h"

struct ira_pec {
	ul_hst_t * hst;

	ul_hs_t * pds[IraPds_Count];

	ira_dt_t dt_void;
	ira_dt_t dt_dt;
	ira_dt_t dt_bool;
	ira_dt_t dt_ints[IraInt_Count];
	ira_dt_t * dt_vec;
	ira_dt_t * dt_ptr;
	ira_dt_t * dt_stct;
	ira_dt_t * dt_arr;
	ira_dt_t * dt_func;

	ira_lo_t * dt_stct_lo;

	struct {
		ira_dt_t * size;
		ira_dt_t * ascii_str;
		ira_dt_t * wide_str;
	} dt_spcl;

	ira_lo_t * root;

	ul_hs_t * ep_name;
};

bool ira_pec_init(ira_pec_t * pec, ul_hst_t * hst);
void ira_pec_cleanup(ira_pec_t * pec);

bool ira_pec_get_dt_vec(ira_pec_t * pec, size_t size, ira_dt_t * body, ira_dt_qual_t qual, ira_dt_t ** out);
bool ira_pec_get_dt_ptr(ira_pec_t * pec, ira_dt_t * body, ira_dt_qual_t qual, ira_dt_t ** out);
bool ira_pec_get_dt_stct(ira_pec_t * pec, size_t elems_size, ira_dt_ndt_t * elems, ira_dt_qual_t qual, ira_dt_t ** out);
bool ira_pec_get_dt_stct_lo(ira_pec_t * pec, ira_lo_t * lo, ira_dt_qual_t qual, ira_dt_t ** out);
bool ira_pec_get_dt_arr(ira_pec_t * pec, ira_dt_t * body, ira_dt_qual_t qual, ira_dt_t ** out);
bool ira_pec_get_dt_func(ira_pec_t * pec, ira_dt_t * ret, size_t args_size, ira_dt_ndt_t * args, ira_dt_t ** out);

bool ira_pec_apply_qual(ira_pec_t * pec, ira_dt_t * dt, ira_dt_qual_t qual, ira_dt_t ** out);

bool ira_pec_make_val_imm_void(ira_pec_t * pec, ira_val_t ** out);
bool ira_pec_make_val_imm_dt(ira_pec_t * pec, ira_dt_t * dt, ira_val_t ** out);
bool ira_pec_make_val_imm_bool(ira_pec_t * pec, bool bool_val, ira_val_t ** out);
bool ira_pec_make_val_imm_int(ira_pec_t * pec, ira_int_type_t int_type, ira_int_t int_val, ira_val_t ** out);
bool ira_pec_make_val_lo_ptr(ira_pec_t * pec, ira_lo_t * lo, ira_val_t ** out);

bool ira_pec_make_val_null(ira_pec_t * pec, ira_dt_t * dt, ira_val_t ** out);
