#include "wa_err.h"
#include "wa_ctx.h"
#include "wa_wnd.h"

HWND wa_wnd_create(wa_ctx_t * ctx, const wchar_t * wnd_cls_name, HWND parent_hw, DWORD style, DWORD style_ex, ...) {
	wa_wnd_cd_t cd = { .ctx = ctx };

	va_start(cd.args, style_ex);

	HWND hw = CreateWindowExW(style_ex, wnd_cls_name, NULL, style, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, parent_hw, NULL, ctx->itnc, &cd);
	
	va_end(cd.args);

	return hw;
}

bool wa_wnd_check_cls(HWND hw, const wchar_t * cls_name) {
	wchar_t buf[WA_WND_CHECK_CLS_NAME_SIZE_MAX + 1];

	size_t cls_name_size = wcslen(cls_name);

	ul_assert(_countof(buf) > cls_name_size);

	int res = GetClassNameW(hw, buf, _countof(buf));

	if (res == 0 || (size_t)res != cls_name_size) {
		return false;
	}

	return wcscmp(buf, cls_name) == 0 ? true : false;
}

void wa_wnd_paint_buf(HWND hw, HDC hdc, void * user_data, wa_wnd_paint_proc_t * paint_proc) {
	RECT rect;

	GetClientRect(hw, &rect);

	HDC hdc_buf;

	HPAINTBUFFER buf = BeginBufferedPaint(hdc, &rect, BPBF_COMPATIBLEBITMAP, NULL, &hdc_buf);

	if (buf != NULL) {
		paint_proc(user_data, hdc_buf, &rect);

		if (EndBufferedPaint(buf, TRUE) != S_OK) {
			wa_err_check();
		}
	}
	else {
		wa_err_check();
	}
}
