#include "pch.h"
#include "lnk_sect.h"
#include "lnk_pel.h"
#include "asm_size.h"
#include "asm_reg.h"
#include "asm_inst.h"
#include "asm_frag.h"

typedef struct sect_info {
	const char * name;

	size_t align;

	uint8_t align_byte;

	bool mem_r, mem_w, mem_e, mem_disc;
} sect_info_t;

static const sect_info_t sect_infos[AsmFrag_Count] = {
	[AsmFragProc] = { .name = ".text", .align = 16, .align_byte = 0xCC, .mem_r = true, .mem_e = true },
	[AsmFragRoData] = { .name = ".rdata", .align = 16, .align_byte = 0x00, .mem_r = true },
	[AsmFragWrData] = { .name = ".data", .align = 16, .align_byte = 0x00, .mem_r = true, .mem_w = true }
};

asm_frag_t * asm_frag_create(asm_frag_type_t type) {
	asm_frag_t * frag = malloc(sizeof(*frag));

	ul_assert(frag != NULL);

	*frag = (asm_frag_t){ .type = type };

	return frag;
}
void asm_frag_destroy(asm_frag_t * frag) {
	if (frag == NULL) {
		return;
	}

	free(frag->insts);
	free(frag);
}

void asm_frag_destroy_chain(asm_frag_t * frag) {
	while (frag != NULL) {
		asm_frag_t * next = frag->next;

		asm_frag_destroy(frag);

		frag = next;
	}
}

void asm_frag_push_inst(asm_frag_t * frag, asm_inst_t * inst) {
	if (frag->insts_size + 1 > frag->insts_cap) {
		ul_arr_grow(&frag->insts_cap, &frag->insts, sizeof(*frag->insts), 1);
	}

	frag->insts[frag->insts_size++] = *inst;
}

static bool fill_sect_info(asm_frag_t * frag, lnk_sect_t * out) {
	if (frag->type == AsmFragNone || frag->type >= AsmFrag_Count) {
		return false;
	}

	const sect_info_t * info = &sect_infos[frag->type];

	out->name = info->name;

	out->data_align = frag->align == 0 ? info->align : frag->align;

	out->data_align_byte = info->align_byte;

	out->mem_r = info->mem_r;
	out->mem_w = info->mem_w;
	out->mem_e = info->mem_e;
	out->mem_disc = info->mem_disc;

	return true;
}

static bool get_imm0_fixup_stype(asm_inst_t * inst, lnk_sect_lp_stype_t * out) {
	*out = LnkSectLpFixupNone;

	switch (inst->opds) {
		case AsmInstOpds_None:
		case AsmInstOpds_Label:
		case AsmInstOpds_Reg:
		case AsmInstOpds_Mem:
		case AsmInstOpds_Reg_Reg:
		case AsmInstOpds_Reg_Mem:
		case AsmInstOpds_Mem_Reg:
			return true;
		case AsmInstOpds_Imm:
		case AsmInstOpds_Reg_Imm:
		case AsmInstOpds_Mem_Imm:
			break;
		default:
			return false;
	}

	switch (inst->imm0_type) {
		case AsmInstImmNone:
		case AsmInstImm8:
		case AsmInstImm16:
		case AsmInstImm32:
		case AsmInstImm64:
			return true;
		case AsmInstImmLabelRel8:
			*out = LnkSectLpFixupRel8;
			break;
		case AsmInstImmLabelRel32:
			*out = LnkSectLpFixupRel32;
			break;
		case AsmInstImmLabelVa64:
			*out = LnkSectLpFixupVa64;
			break;
		case AsmInstImmLabelRva32:
			*out = LnkSectLpFixupRva32;
			break;
		default:
			return false;
	}

	return true;
}
static bool get_disp_fixup_stype(asm_inst_t * inst, lnk_sect_lp_stype_t * out) {
	*out = LnkSectLpFixupNone;

	switch (inst->opds) {
		case AsmInstOpds_None:
		case AsmInstOpds_Label:
		case AsmInstOpds_Imm:
		case AsmInstOpds_Reg:
		case AsmInstOpds_Reg_Imm:
		case AsmInstOpds_Reg_Reg:
			return true;
		case AsmInstOpds_Mem:
		case AsmInstOpds_Mem_Imm:
		case AsmInstOpds_Reg_Mem:
		case AsmInstOpds_Mem_Reg:
			break;
		default:
			return false;
	}

	switch (inst->mem_disp_type) {
		case AsmInstDispAuto:
		case AsmInstDispNone:
		case AsmInstDisp8:
		case AsmInstDisp32:
			return true;
		case AsmInstDispLabelRel8:
			*out = LnkSectLpFixupRel8;
			break;
		case AsmInstDispLabelRel32:
			*out = LnkSectLpFixupRel32;
			break;
		case AsmInstDispLabelRva32:
			*out = LnkSectLpFixupRva32;
			break;
		default:
			return false;
	}

	return true;
}

static bool build_special_inst(asm_frag_t * frag, asm_inst_t * inst, lnk_sect_t * out) {
	switch (inst->type) {
		case AsmInstLabel:
			if (inst->opds != AsmInstOpds_Label) {
				return false;
			}

			lnk_sect_add_lp(out, LnkSectLpLabel, LnkSectLpLabelNone, inst->label, out->data_size);

			break;
		case AsmInstAlign:
		{
			if (inst->opds != AsmInstOpds_Imm || inst->imm0_type != AsmInstImm64) {
				return false;
			}

			size_t cur_size = out->data_size;

			size_t aligned_size = ul_align_to(cur_size, (size_t)inst->imm0);

			if (aligned_size >= LNK_PEL_MODULE_MAX_SIZE) {
				return false;
			}

			if (aligned_size > out->data_cap) {
				ul_arr_grow(&out->data_cap, &out->data, sizeof(*out->data), aligned_size - out->data_cap);
			}

			memset(out->data + out->data_size, out->data_align_byte, aligned_size - cur_size);

			out->data_size = aligned_size;

			break;
		}
		default:
			return false;
	}

	return true;
}
static bool build_core(asm_frag_t * frag, lnk_sect_t * out) {
	if (!fill_sect_info(frag, out)) {
		return false;
	}

	for (asm_inst_t * inst = frag->insts, *inst_end = inst + frag->insts_size; inst != inst_end; ++inst) {
		if (build_special_inst(frag, inst, out)) {
			continue;
		}

		size_t inst_size;
		asm_inst_bs_t inst_bs;
		asm_inst_offs_t inst_offs;

		if (!asm_inst_build(inst, &inst_size, inst_bs, &inst_offs)) {
			return false;
		}

		if (out->data_size + inst_size > out->data_cap) {
			ul_arr_grow(&out->data_cap, &out->data, sizeof(*out->data), out->data_size + inst_size - out->data_cap);
		}

		size_t inst_start = out->data_size;

		memcpy(out->data + out->data_size, inst_bs, inst_size);
		out->data_size += inst_size;

		lnk_sect_lp_stype_t stype;

		if (!get_imm0_fixup_stype(inst, &stype)) {
			return false;
		}
		else if (stype != LnkSectLpFixupNone) {
			lnk_sect_add_lp(out, LnkSectLpFixup, stype, inst->imm0_label, inst_start + inst_offs.off[AsmInstOffImm]);
		}

		if (!get_disp_fixup_stype(inst, &stype)) {
			return false;
		}
		else if (stype != LnkSectLpFixupNone) {
			lnk_sect_add_lp(out, LnkSectLpFixup, stype, inst->mem_disp_label, inst_start + inst_offs.off[AsmInstOffDisp]);
		}
	}

	return true;
}

bool asm_frag_build(asm_frag_t * frag, lnk_sect_t ** out) {
	*out = lnk_sect_create();

	return build_core(frag, *out);
}
