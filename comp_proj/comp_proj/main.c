#include "pch.h"
#include "wa_lib/wa_ctx.h"
#include "wa_lib/wa_wnd.h"
#include "wa_lib/wa_host.h"
#include "wa_lib/wa_wcr.h"
#include "pla_edit.h"
#include "pla_ast_p.h"
#include "pla_ast_t.h"
#include "ira_pec_c.h"
#include "asm_pea_b.h"
#include "lnk_pe_l.h"

static const wchar_t * pla_name = L"test.pla";
static const wchar_t * exe_name = L"test.exe";
static const wchar_t * exe_cmd = L"test.exe";

static ul_hst_t main_hst;
static wa_ctx_t main_wa_ctx;
static wa_wcr_t main_wcr;
static lnk_pe_t main_pe;
static asm_pea_t main_pea;
static ira_pec_t main_pec;
static pla_ast_t main_ast;

static bool main_gui() {
	if (!wa_ctx_init(&main_wa_ctx, &main_hst)) {
		return false;
	}

	if (!wa_wcr_init(&main_wcr, &main_wa_ctx)) {
		return false;
	}

	if (!wa_wcr_register_p(&main_wcr, pla_edit_get_wnd_cls_desc)) {
		return false;
	}

	ul_hs_t * main_wnd_name = UL_HST_HASHADD_WS(&main_hst, L"main window");

	HWND main_wnd = wa_wnd_create(&main_wa_ctx, L"wa_host", NULL, WS_OVERLAPPEDWINDOW, WS_EX_COMPOSITED, L"size", wa_host_dflt_size, L"text", main_wnd_name, NULL);

	if (main_wnd == NULL) {
		return false;
	}

	HWND pla_edit = wa_wnd_create(&main_wa_ctx, L"pla_edit", main_wnd, WS_CHILD | WS_VISIBLE, 0, NULL);

	if (pla_edit == NULL) {
		return false;
	}

	wa_ctx_use_show_cmd(&main_wa_ctx, main_wnd);

	wa_ctx_run_msg_loop(&main_wa_ctx);

	return true;
}
static bool main_parse() {
	bool result = pla_ast_p_parse(&main_hst, pla_name, &main_ast);

	wprintf(L"parsing result: %s\n", result ? L"success" : L"failure");

	return result;
}
static bool main_translate() {
	bool result = pla_ast_t_translate(&main_ast, &main_pec);

	wprintf(L"translation result: %s\n", result ? L"success" : L"failure");

	return result;
}
static bool main_compile() {
	bool result = ira_pec_c_compile(&main_pec, &main_pea);

	wprintf(L"compilation result: %s\n", result ? L"success" : L"failure");

	return result;
}
static bool main_assemble() {
	bool result = asm_pea_b_build(&main_pea, &main_pe);

	wprintf(L"assembling result: %s\n", result ? L"success" : L"failure");

	return result;
}
static bool main_link() {
	main_pe.file_name = exe_name;

	bool result = lnk_pe_l_link(&main_pe);

	wprintf(L"link result: %s\n", result ? L"success" : L"failure");

	return result;
}
static void main_run() {
	int ret_code = (int)_wspawnl(_P_WAIT, exe_name, exe_cmd, NULL);

	wprintf(L"program exit code: %d 0x%X\n", ret_code, ret_code);
}

static int main_core() {
	ul_hst_init(&main_hst);
	
	if (!main_gui()) {
		return -1;
	}

	/*
	if (!main_parse()) {
		return -1;
	}

	if (!main_translate()) {
		return -1;
	}

	if (!main_compile()) {
		return -1;
	}

	if (!main_assemble()) {
		return -1;
	}

	if (!main_link()) {
		return -1;
	}

	main_run();

	*/

	return 0;
}

int main() {
	int res;
	
	__try {
		res = main_core();
	}
	__finally {
		wa_wcr_cleanup(&main_wcr);

		wa_ctx_cleanup(&main_wa_ctx);

		/*
		lnk_pe_cleanup(&main_pe);

		asm_pea_cleanup(&main_pea);

		ira_pec_cleanup(&main_pec);

		pla_ast_cleanup(&main_ast);
		*/

		ul_hst_cleanup(&main_hst);
	}

	_CrtDumpMemoryLeaks();

	return res;
}