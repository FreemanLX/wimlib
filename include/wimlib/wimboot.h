#ifndef _WIMBOOT_H_
#define _WIMBOOT_H_

#include "wimlib/sha1.h"
#include "wimlib/types.h"
#include "wimlib/header.h"

struct wim_lookup_table_entry;

extern int
wimboot_alloc_data_source_id(const wchar_t *wim_path,
			     const u8 guid[WIM_GUID_LEN], int image,
			     const wchar_t *target, u64 *data_source_id_ret,
			     bool *wof_running_ret);

extern int
wimboot_set_pointer(OBJECT_ATTRIBUTES *attr,
		    const wchar_t *printable_path,
		    const struct wim_lookup_table_entry *lte,
		    u64 data_source_id,
		    const u8 lookup_table_hash[SHA1_HASH_SIZE],
		    bool wof_running);


#endif /* _WIMBOOT_H_ */
