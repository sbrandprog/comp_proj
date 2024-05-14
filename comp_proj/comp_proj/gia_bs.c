#include "pch.h"
#include "lnk_pe_l.h"
#include "asm_pea_b.h"
#include "ira_pec_c.h"
#include "pla_ec_buf.h"
#include "pla_lex.h"
#include "pla_ast_t.h"
#include "gia_repo.h"
#include "gia_bs_p.h"

typedef struct gia_bs_ctx {
	gia_repo_t * repo;
	ul_hs_t * first_tus_name;
	const wchar_t * file_name;

	pla_ec_buf_t pla_ec_buf;
	pla_lex_t pla_lex;
	pla_ast_t pla_ast;
	ira_pec_t ira_pec;
	asm_pea_t asm_pea;
	lnk_pe_t lnk_pe;
} ctx_t;

static bool build_core(ctx_t * ctx) {
	pla_ec_buf_init(&ctx->pla_ec_buf);
	
	pla_lex_init(&ctx->pla_lex, &ctx->repo->hst, &ctx->pla_ec_buf.ec);

	if (!gia_bs_p_form_ast_nl(ctx->repo, ctx->first_tus_name, &ctx->pla_lex, &ctx->pla_ast)) {
		return false;
	}

	if (!pla_ast_t_translate(&ctx->pla_ast, &ctx->ira_pec)) {
		return false;
	}

	if (!ira_pec_c_compile(&ctx->ira_pec, &ctx->asm_pea)) {
		return false;
	}

	if (!asm_pea_b_build(&ctx->asm_pea, &ctx->lnk_pe)) {
		return false;
	}

	ctx->lnk_pe.file_name = ctx->file_name;

	if (!lnk_pe_l_link(&ctx->lnk_pe)) {
		return false;
	}

	return true;
}
bool gia_bs_build_nl(gia_repo_t * repo, ul_hs_t * first_tus_name, const wchar_t * file_name) {
	ctx_t ctx = { .repo = repo, .first_tus_name = first_tus_name, .file_name = file_name };

	bool res;

	__try {
		res = build_core(&ctx);
	}
	__finally {
		lnk_pe_cleanup(&ctx.lnk_pe);

		asm_pea_cleanup(&ctx.asm_pea);

		ira_pec_cleanup(&ctx.ira_pec);

		pla_ast_cleanup(&ctx.pla_ast);

		pla_lex_cleanup(&ctx.pla_lex);

		pla_ec_buf_cleanup(&ctx.pla_ec_buf);
	}

	return res;
}
