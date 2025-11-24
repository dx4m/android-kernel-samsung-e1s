#ifndef __MIF_REQUESTER_H__
#define __MIF_REQUESTER_H__

struct mif_requester {
	const char **mif_requester_names;
	unsigned int *mif_requester_durations;
	unsigned int mif_master_num;
};

extern int get_mif_requester_info(struct mif_requester **mif_requester);
extern int reset_mif_requester_duration(void);

#endif
