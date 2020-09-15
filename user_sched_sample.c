#include <stdlib.h>
#include <wd_comp.h>


#define MAX_CTX_NUM 1024

enum sched_x_pos {
	X_SYNC = 0,
	X_ASYNC = 1,
	X_BUTT
};

enum sched_y_pos {
	Y_COMP = 0,
	Y_UNCOMP = 1,
	Y_BUTT
};

/**
 * struct sched_ctx_range - define one ctx pos.
 * @begin: the start pos in ctxs of config.
 * @end: the end pos in ctxx of config.
 * @last: the last one which be distributed
 */
struct sched_ctx_region {
	int begin;
	int end;
	int last;
};

/**
 * sample_sched_info - define the context of the scheduler
 * @ctx_region: define the map for the comp ctxs, using for quickly search
				the x range: two(sync and async), the y range: two(comp and uncomp)
				the map[x][y]'s value is the ctx begin and end pos
 * @last_pos: record the last one pos which be distributed in different type ctxs
 * @count: record the req count of ccontex
 */
struct sample_sched_info {
	struct sched_ctx_region ctx_region[X_BUTT][Y_BUTT];
	int count[MAX_CTX_NUM];
};


int sample_get_next_pos(struct sched_ctx_region *region) {
	int pos = region->last;

	if (pos < region->last) {
		pos++;
	} else if (pos == region->last) {
		pos = region->begin;
	} else {
		/* If the pos's value is out of range, we can output the error info and correct the error */
		printf("ERROR: pos = %d, begin = %d, end = %d", pos, region->begin, region->end);
		pos = region->begin;
	}

	region->last = pos;

	return pos;
}

/**
 * sample_get_ctx_range - Get ctx range from ctx_map by the wd comp arg
 */
struct sched_ctx_region *sample_get_ctx_range(struct wd_comp_arg *arg, struct sched_ctx_region **ctx_map)
{
	return NULL;
}

/**
 * ssample_pick_next_ctx - Get one ctx from ctxs by the sched_ctx and arg
 *
 * This function will be registered to the wd comp
 */
handle_t sample_pick_next_ctx(struct wd_ctx_config *cfg, void* sched_ctx, struct wd_comp_arg *arg)
{
	int pos;
	struct sched_ctx_region *region = NULL;
	struct sample_sched_info *sched_info = (struct sample_sched_info*)sched_ctx;

	region = sample_get_ctx_range(arg, sched_info->ctx_region);
	if (region) {
		return NULL;
	}

	pos = sample_get_next_pos(region);

	return cfg->ctxs[pos].ctx;
}

/**
 * sample_poll_policy - The polling policy matches the pick next ctx
 *
 * This function will be registered to the wd comp 
 */
__u32 sample_poll_policy(struct wd_ctx_config *cfg, void* sched_ctx)
{
	return 0;
}

/**
 * sample_sched_init - initialize the global sched info
 */
void sample_sched_init(void *sched_ctx)
{
	struct sample_sched_info *sched_info = (struct sample_sched_info*)sched_ctx;

	memset(sched_info, 0, sizeof(struct sample_sched_info));

	/* initialize the global sched info base the ctx allocation */

	return;
}

