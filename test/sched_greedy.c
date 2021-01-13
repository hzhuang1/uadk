// SPDX-License-Identifier: Apache-2.0
//
// SCHED_GREEDY always tries to pick up an unused context.
//

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sched_greedy.h"

#define MAX_POLL_TIMES 1000

enum sched_region_mode {
	SCHED_MODE_SYNC = 0,
	SCHED_MODE_ASYNC,
	SCHED_MODE_MAX,
};

/**
 * struct sched_ctx_range - define one ctx pos.
 * @begin: the start pos in ctxs of config.
 * @end: the end pos in ctxx of config.
 * @last: the last one which be distributed.
 */
struct sched_ctx_region {
	struct wd_ctx *ctx_pool;
	int num;
	int last;
	int rgn_idx;
	bool valid;
	pthread_mutex_t lock;
};

struct sched_greedy_info {
	__u32 type_num;
	__u8  numa_num;
	user_poll_func poll_func;
	struct sched_ctx_region *region;
};

static inline struct sched_ctx_region *get_region(handle_t h_sched, __u8 type,
						  enum sched_region_mode mode,
						  __u8 numa)
{
	struct sched_greedy_info *info = (struct sched_greedy_info *)h_sched;
	int i;

	if (unlikely((numa >= info->numa_num) || (type >= info->type_num) ||
	    (mode >= SCHED_MODE_MAX)))
		return NULL;
	i = type * SCHED_MODE_MAX * info->numa_num + mode * info->numa_num +
		numa;
	return &info->region[i];
}

int get_pos_in_region(struct sched_ctx_region *region)
{
	int i, off, found, ret;

	if (!region->ctx_pool || !region->num) {
		return -EINVAL;
	}
	for (i = 0, found = 0; !found && i < region->num; i++) {
		off = (i + 1 + region->last) % region->num;
		ret = pthread_mutex_trylock(&region->ctx_pool[off].lock);
		if (ret)
			continue;
		found = 1;
		region->last = off;
	}
	if (!found) {
		off = (i + 1 + region->last) % region->num;
		pthread_mutex_lock(&region->ctx_pool[off].lock);
		region->last = off;
	}
	return region->last;
}

__u32 convert_unique_pos(handle_t h_sched, int region_idx, int offs)
{
	struct sched_greedy_info *info = (struct sched_greedy_info *)h_sched;
	int i, ret;

	for (i = 0, ret = 0; i < region_idx; i++)
		ret += info->region[i].num;
	ret += offs;
	return (__u32)ret;
}

/**
 * sched_greedy_pick_next - Get one ctx from ctxs by the sched_ctx and arg.
 * @sched_ctx: Schedule ctx, reference the struct sched_greedy_ctx.
 * @cfg: The global resoure info.
 * @reg: The service request msg, different algorithm should support analysis
 *       function.
 * @key: The key of schedule region.
 *
 * The user must init the schdule info through sched_greedy_fill_data, the
 * func interval will not check the valid, becouse it will affect performance.
 */
static __u32 sched_greedy_pick_next(handle_t h_sched, const void *req,
					const struct sched_key *key)
{
	struct sched_greedy_info *info = (struct sched_greedy_info *)h_sched;
	struct sched_ctx_region *region;
	int i, offs;

	if (!key || !req) {
		WD_ERR("ERROR: %s the pointer para is NULL !\n", __func__);
		return INVALID_POS;
	}

	region = get_region(h_sched, key->type, key->mode, key->numa_id);
	if (!region->num) {
		/* get context from other NUMA node instead */
		for (i = 0; i < info->numa_num; i++) {
			region = get_region(h_sched, key->type, key->mode, i);
			if (region->num)
				break;
		}
	}

	offs = get_pos_in_region(region);
	if (offs < 0) {
		return INVALID_POS;
	}
	return convert_unique_pos(h_sched, region->rgn_idx, offs);
}

static void sched_greedy_put_ctx(handle_t h_sched, __u32 pos)
{
	struct sched_greedy_info *info = (struct sched_greedy_info *)h_sched;
	struct wd_ctx *ctx;
	int i, sum, max, offs;

	max = info->numa_num * info->type_num * SCHED_MODE_MAX;
	for (i = 0, sum = 0; i < max; i++) {
		sum += info->region[i].num;
		if (sum >= pos) {
			sum = sum - info->region[i].num;
			offs = pos - sum;
			ctx = &info->region[i].ctx_pool[offs];
			if (ctx)
				pthread_mutex_unlock(&ctx->lock);
		}
	}
}

/**
 * sample_poll_policy - The polling policy matches the pick next ctx.
 * @sched_ctx: Schedule ctx, reference the struct sched_greedy_ctx.
 * @cfg: The global resoure info.
 * @expect: User expect poll msg num.
 * @count: The actually poll num.
 *
 * The user must init the schdule info through sched_greedy_fill_data, the
 * func interval will not check the valid, becouse it will affect performance.
 */
static int sched_greedy_poll_policy(handle_t h_sched, __u32 expect,
				    __u32 *count)
{
	struct sched_greedy_info *info = (struct sched_greedy_info *)h_sched;
	struct sched_ctx_region *region;
	int loop, type, numa, offs;
	int ret = 0;
	__u32 pos;

	if (!count) {
		WD_ERR("ERROR: %s the para is NULL !\n", __func__);
		return -EINVAL;
	}
	if (!expect)
		return 0;

	for (loop = 0; loop < MAX_POLL_TIMES; loop++) {
		for (type = 0; type < info->type_num; type++) {
			for (numa = 0; numa < info->numa_num; numa++) {
				region = get_region(h_sched, type,
						    SCHED_MODE_ASYNC,
						    numa);
				if (!region->num)
					continue;
				for (offs = 0; offs < region->num; offs++) {
					pos = convert_unique_pos(h_sched,
								 region->rgn_idx,
								 offs);
					ret = info->poll_func(pos, expect,
							      count);
					//WD_ERR("#%s, %d, loop:%d, type:%d, numa:%d, offs:%d, expect:%d, count:%d, ret:%d\n",
					//	__func__, __LINE__, loop, type, numa, offs, expect, *count, ret);
					if ((ret < 0) && (ret != -EAGAIN))
						return ret;
					else if (ret == -EAGAIN)
						continue;
					if (*count >= expect)
						return 0;
				}
			}
		}
	}

	return 0;
}

struct sched_greedy_table {
	const char *name;
	enum sched_policy_type type;
	__u32 (*pick_next_ctx)(handle_t h_sched_ctx, const void *req,
			       const struct sched_key *key);
	void (*put_ctx)(handle_t h_sched_ctx, __u32 pos);
	int (*poll_policy)(handle_t h_sched_ctx,
			   __u32 expect,
			   __u32 *count);
} sched_greedy_table[SCHED_POLICY_MAX] = {
	{
		.name = "Greedy scheduler",
		.type = SCHED_POLICY_GREEDY,
		.pick_next_ctx = sched_greedy_pick_next,
		.put_ctx = sched_greedy_put_ctx,
		.poll_policy = sched_greedy_poll_policy,
	},
};

struct wd_sched *sched_greedy_alloc(__u8 type_num, __u8 numa_num,
				    user_poll_func func) 
{
	struct sched_greedy_info *info;
	struct sched_ctx_region *region;
	struct wd_sched *sched;
	int region_num;
	int i;

	if (!type_num) {
		WD_ERR("Error: %s type_num = %u is invalid!\n",
		       __func__, type_num);
		return NULL;
	}

	if (!func) {
		WD_ERR("Error: %s poll_func is null!\n", __func__);
		return NULL;
	}

	if (!numa_num) {
		WD_ERR("Warning: %s set numa number as %d!\n", __func__,
		       MAX_NUMA_NUM);
		numa_num = MAX_NUMA_NUM;
	}

	sched = calloc(1, sizeof(struct wd_sched));
	if (!sched) {
		WD_ERR("Error: %s wd_sched alloc error!\n", __func__);
		return NULL;
	}

	region_num = numa_num * type_num * SCHED_MODE_MAX;
	info = calloc(1, sizeof(struct sched_greedy_info));
	if (!info) {
		WD_ERR("Error: %s fail to allocate sched_info!\n", __func__);
		goto out;
	}

	region = calloc(1, region_num * sizeof(struct sched_ctx_region));
	if (!region) {
		WD_ERR("Error: %s fail to allocate region!\n", __func__);
		goto out_region;
	}
	for (i = 0; i < region_num; i++)
		region[i].rgn_idx = i;
	info->region = region;
	info->poll_func = func;
	info->type_num = type_num;
	info->numa_num = numa_num;

	sched->name = strdup(sched_greedy_table[0].name);
	sched->pick_next_ctx = sched_greedy_table[0].pick_next_ctx;
	sched->put_ctx = sched_greedy_table[0].put_ctx;
	sched->poll_policy = sched_greedy_table[0].poll_policy;
	sched->h_sched_ctx = (handle_t)info;

	return sched;

out_region:
	free(info);
out:
	free(sched);
	return NULL;
}

void sched_greedy_free(struct wd_sched *sched)
{
	struct sched_greedy_info *info;
	int i, max;

	if (unlikely(!sched))
		return;
	info = (struct sched_greedy_info *)sched->h_sched_ctx;
	max = info->numa_num * info->type_num * SCHED_MODE_MAX;
	for (i = 0; i < max; i++) {
		if (info->region[i].ctx_pool) {
			free(info->region[i].ctx_pool);
		}
	}
	if (info->region)
		free(info->region);
	free(info);
	free(sched);
}

int sched_greedy_bind_ctx(struct wd_sched *sched, __u8 numa, __u8 type,
			  __u8 mode, struct wd_ctx *ctxs, __u8 num)
{
	struct sched_greedy_info *info;
	struct sched_ctx_region *region;
	struct wd_ctx *ctx_pool;
	int i, ret;

	info = (struct sched_greedy_info *)sched->h_sched_ctx;
	if (unlikely((numa >= info->numa_num) || (type >= info->type_num) ||
	    (mode >= SCHED_MODE_MAX) || !ctxs || !num))
		return -EINVAL;
	ctx_pool = calloc(1, sizeof(struct wd_ctx) * num);
	if (!ctx_pool)
		return -ENOMEM;
	memcpy(ctx_pool, ctxs, num * sizeof(struct wd_ctx));
	for (i = 0; i < num; i++) {
		pthread_mutex_init(&ctx_pool[i].lock, NULL);
	}
	region = get_region(sched->h_sched_ctx, type, mode, numa);
	if (!region) {
		ret = -EINVAL;
		goto out;
	}
	region->ctx_pool = ctx_pool;
	region->num = num;
	region->valid = true;
	pthread_mutex_init(&region->lock, NULL);
	return 0;
out:
	free(ctx_pool);
	return ret;
}
