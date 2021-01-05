// SPDX-License-Identifier: Apache-2.0
#ifndef SCHED_GREEDY_H

#define SCHED_GREEDY_H

#include "wd_alg_common.h"

#define MAX_NUMA_NUM 4
#define INVALID_POS 0xFFFFFFFF


/* The global policy type */
enum sched_policy_type {
	/* requests will be sent to ctxs one by one */
	SCHED_POLICY_GREEDY = 0,
	SCHED_POLICY_MAX
};

typedef int (*user_poll_func)(__u32 pos, __u32 expect, __u32 *count);

/*
 * sched_greedy_fill_data - Fill the schedule min region.
 * @sched: The schdule instance
 * @numa_id: NUMA ID
 * @mode: Sync or async mode. sync: 0, async: 1
 * @type: Service type, the value must smaller than type_num.
 * @begin: The begig ctx resource index for the region
 * @end:  The end ctx resource index for the region.
 *
 * The shedule indexed mode is NUMA -> MODE -> TYPE -> [BEGIN : END],
 * then select one index from begin to end.
 */
int sched_greedy_bind_ctx(struct wd_sched *sched, __u8 numa, __u8 type,
			  __u8 mode, struct wd_ctx *ctxs, __u8 num);

/**
 * sched_greedy_alloc - Allocate a schedule instance.
 * @sched_type: Reference sched_policy_type.
 * @type_num: The service type num of user's service. For example, the zip
 *            include comp and un comp, type nume is two.
 * @numa_num: The number of numa that the user needs.
 * @func: The ctx poll function of user underlying operating.
 *
 */
struct wd_sched *sched_greedy_alloc(__u8 type_num, __u8 numa_num,
				    user_poll_func func);

/**
 * sched_greedy_release - Release schedule memory.
 * &sched: The schedule which will be released.
 */
void sched_greedy_free(struct wd_sched *sched);

#endif
