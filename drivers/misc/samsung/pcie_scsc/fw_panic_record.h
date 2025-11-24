/****************************************************************************
 *
 * Copyright (c) 2014 - 2016 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef FW_PANIC_RECORD_H__
#define FW_PANIC_RECORD_H__
#include "scsc_mif_abs.h"

enum cores_type {
	R4,
	R7,
	M3,
	M4,
	M7,
	UCPU0,
	UCPU1,
	UCPU2,
	UCPU3
};

static const char *core_names[] = {
    "R4", "R7", "M3", "M4", "M7", "ucpu0", "ucpu1", "ucpu2", "ucpu3"
};

bool fw_parse_r4_panic_record(u32 *r4_panic_record, u32 *r4_panic_record_length,
			      u32 *r4_panic_stack_record_offset, bool dump);
bool fw_parse_r4_panic_stack_record(u32 *r4_panic_stack_record, u32 *r4_panic_stack_record_length, bool dump);
bool fw_parse_m4_panic_record(u32 *m4_panic_record, u32 *m4_panic_record_length, bool dump);

bool fw_parse_get_r4_sympathetic_panic_flag(u32 *r4_panic_record);
bool fw_parse_get_m4_sympathetic_panic_flag(u32 *m4_panic_record);

bool fw_parse_wpan_panic_record(u32 *wpan_panic_record, u32 *full_panic_code, bool dump);

int panic_record_dump_buffer(char *processor, u32 *panic_record,
			     u32 panic_record_length, char *buffer, size_t blen);
#if defined(CONFIG_SCSC_BB_REDWOOD)
bool fw_parse_ucpu_panic_record(u32 *ucpu_panic_record, u32 *ucpu_panic_record_length,
				u32 *ucpu_panic_stack_record_offset, bool dump, enum scsc_mif_abs_target target);
bool fw_parse_ucpu_panic_stack_record(u32 *ucpu_panic_stack_record, u32 *ucpu_panic_stack_record_length,
				bool dump, enum scsc_mif_abs_target target);
bool fw_parse_get_ucpu_sympathetic_panic_flag(u32 *ucpu_panic_record);
#endif
#endif /* FW_PANIC_RECORD_H__ */
