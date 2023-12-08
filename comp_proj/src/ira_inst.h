#pragma once
#include "ira.h"
#include "u_ros.h"
#include "u_hs.h"

#define IRA_INST_OPDS_SIZE 5

enum ira_inst_opd_type {
	IraInstOpdNone,
	IraInstOpdDt,
	IraInstOpdVal,
	IraInstOpdVarDef,
	IraInstOpdVar,
	IraInstOpdMmbr,
	IraInstOpdVarsSize,
	IraInstOpdVars2,
	IraInstOpdIds2,
	IraInstOpds_Count
};
union ira_inst_opd {
	size_t size;
	u_hs_t * hs;
	u_hs_t ** hss;
	ira_dt_t * dt;
	ira_val_t * val;
};

enum ira_inst_type {
	IraInstNone,
#define IRA_INST(name, ...) IraInst##name,
#include "ira_inst.def"
#undef IRA_INST
	IraInst_Count
};
struct ira_inst {
	ira_inst_type_t type;
	union {
		ira_inst_opd_t opds[IRA_INST_OPDS_SIZE];
		struct {
			ira_inst_opd_t opd0;
			ira_inst_opd_t opd1;
			ira_inst_opd_t opd2;
			ira_inst_opd_t opd3;
			ira_inst_opd_t opd4;
		};
	};
};

struct ira_inst_info {
	u_ros_t type_str;
	bool rt_comp, ct_comp;
	union {
		ira_inst_opd_type_t opds[IRA_INST_OPDS_SIZE];
		struct {
			ira_inst_opd_type_t opd0, opd1, opd2, opd3, opd4;
		};
	};
};

void ira_inst_cleanup(ira_inst_t * inst);

const ira_inst_info_t ira_inst_infos[IraInst_Count];
