// SPDX-License-Identifier: Apache-2.0

#include "test_lib.h"

static void *sw_dfl_sw_ifl(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	void *tbuf;
	size_t tbuf_sz;
	comp_md5_t final_md5;
	int i, ret;

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = malloc(tbuf_sz);
	if (!tbuf)
		return (void *)(uintptr_t)(-ENOMEM);

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = sw_deflate(tdata->src, tbuf, tdata->src_sz, opts);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out;
		}
		ret = sw_inflate(tbuf, tdata->dst, tbuf_sz, opts);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out;
		}
		ret = calculate_md5(&final_md5, tdata->dst, tdata->dst_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
			goto out;
		}
		ret = cmp_md5(&tdata->md5, &final_md5);
		if (ret) {
			printf("MD5 is unmatched (%d) at %dth times on "
				"thread %d\n", ret, i, tdata->tid);
			goto out;
		}
	}
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	return NULL;
out:
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	return (void *)(uintptr_t)(ret);
}

static void *sw_dfl_hw_ifl(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_ifl;
	void *tbuf;
	size_t tbuf_sz;
	comp_md5_t final_md5;
	int i, ret;
	struct timeval start_tvl, end_tvl;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl)
		return (void *)(uintptr_t)(-EINVAL);

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = malloc(tbuf_sz);
	if (!tbuf) {
		ret = -ENOMEM;
		goto out;
	}

	gettimeofday(&start_tvl, NULL);
	for (i = 0; i < opts->compact_run_num; i++) {
		ret = sw_deflate(tdata->src, tbuf, tdata->src_sz, opts);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = hw_inflate(h_ifl, tbuf, tdata->dst, tbuf_sz,
				 opts, &tdata->sem);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&final_md5, tdata->dst, tdata->dst_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
			goto out_run;
		}
		ret = cmp_md5(&tdata->md5, &final_md5);
		if (ret) {
			printf("MD5 is unmatched (%d) at %dth times on "
				"thread %d\n", ret, i, tdata->tid);
			goto out_run;
		}
	}
	gettimeofday(&end_tvl, NULL);
	wd_comp_free_sess(h_ifl);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	wd_comp_free_sess(h_ifl);
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
out:
	return (void *)(uintptr_t)(ret);
}

static void *hw_dfl_sw_ifl(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_dfl;
	void *tbuf;
	size_t tbuf_sz;
	comp_md5_t final_md5;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = malloc(tbuf_sz);
	if (!tbuf) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_deflate(h_dfl, tdata->src, tbuf, tdata->src_sz,
				 opts, &tdata->sem);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = sw_inflate(tbuf, tdata->dst, tbuf_sz, opts);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&final_md5, tdata->dst, tdata->dst_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
			goto out_run;
		}
		ret = cmp_md5(&tdata->md5, &final_md5);
		if (ret) {
			printf("MD5 is unmatched (%d) at %dth times on "
				"thread %d\n", ret, i, tdata->tid);
			goto out_run;
		}
	}
	wd_comp_free_sess(h_dfl);
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	wd_comp_free_sess(h_dfl);
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
out:
	return (void *)(uintptr_t)(ret);
}

static void *hw_dfl_hw_ifl(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_dfl, h_ifl;
	void *tbuf;
	size_t tbuf_sz;
	comp_md5_t final_md5;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	setup.op_type = WD_DIR_DECOMPRESS;
	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl) {
		ret = -EINVAL;
		goto out;
	}

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = malloc(tbuf_sz);
	if (!tbuf) {
		ret = -ENOMEM;
		goto out_buf;
	}

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_deflate(h_dfl, tdata->src, tbuf, tdata->src_sz,
				 opts, &tdata->sem);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = hw_inflate(h_ifl, tbuf, tdata->dst, tbuf_sz,
				 opts, &tdata->sem);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&final_md5, tdata->dst, tdata->dst_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
			goto out_run;
		}
		ret = cmp_md5(&tdata->md5, &final_md5);
		if (ret) {
			printf("MD5 is unmatched (%d) at %dth times on "
				"thread %d\n", ret, i, tdata->tid);
			goto out_run;
		}
	}
	wd_comp_free_sess(h_dfl);
	wd_comp_free_sess(h_ifl);
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	free(tbuf);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
out_buf:
	wd_comp_free_sess(h_ifl);
out:
	wd_comp_free_sess(h_dfl);
	return (void *)(uintptr_t)(ret);
}

static void *hw_dfl_perf(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_dfl;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_deflate(h_dfl, tdata->src, tdata->dst, tdata->src_sz,
				 opts, &tdata->sem);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_dfl);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	wd_comp_free_sess(h_dfl);
	return (void *)(uintptr_t)(ret);
}

static void *hw_ifl_perf(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_ifl;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_inflate(h_ifl, tdata->src, tdata->dst, tdata->src_sz,
				 opts, &tdata->sem);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_ifl);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	wd_comp_free_sess(h_ifl);
	return (void *)(uintptr_t)(ret);
}

/* BATCH mode is used */
static void *hw_dfl_perf2(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_dfl;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_deflate2(h_dfl, tdata->src, tdata->dst, tdata->src_sz,
				  tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_dfl);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	wd_comp_free_sess(h_dfl);
	return (void *)(uintptr_t)(ret);
}

/* BATCH mode is used */
static void *hw_ifl_perf2(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	handle_t h_ifl;
	int i, ret;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		ret = hw_inflate2(h_ifl, tdata->src, tdata->dst, tdata->src_sz,
				  tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_ifl);
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	free(tdata->dst);
	ret = __atomic_sub_fetch(&info->in_share, 1, __ATOMIC_SEQ_CST);
	if (!ret)
		free(tdata->src);
	wd_comp_free_sess(h_ifl);
	return (void *)(uintptr_t)(ret);
}

/* Only support SYNC mode */
static int test_sw_dfl_sw_ifl(void)
{
	struct hizip_test_info info = {0};
	struct test_options opts = {
		.alg_type		= WD_ZLIB,
		.sync_mode		= 0,
		.thread_num		= 16,
		.block_size		= 8192,
		.total_len		= 8192 * 10,
		.compact_run_num	= 1000,
	};
	struct timeval start_tvl, end_tvl;
	double ilen, usec, speed;
	int ret;

	info.opts = &opts;
	info.in_size = opts.total_len;
	info.out_size = opts.total_len;
	ret = create_send2_threads(&opts, &info, sw_dfl_sw_ifl);
	if (ret)
		return ret;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(&opts, &info);
	if (ret)
		return ret;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts.total_len * opts.thread_num * opts.compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	printf("Mixture of SW compress and SW decompress with %d threads "
	       "at %.2fMB/s in %f usec.\n", opts.thread_num, speed, usec);
	free_threads(&info);
	return 0;
}

static int test_sw_dfl_hw_ifl(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send2_threads(opts, &info, sw_dfl_hw_ifl);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads",
			opts->thread_num, opts->poll_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("Mixture of SW compress and HW %s decompress with %s "
	       "at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_dfl_sw_ifl(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send2_threads(opts, &info, hw_dfl_sw_ifl);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads",
			opts->thread_num, opts->poll_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("Mixture of HW %s compress and SW decompress with %s "
	       "at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_dfl_hw_ifl(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send2_threads(opts, &info, hw_dfl_hw_ifl);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads",
			opts->thread_num, opts->poll_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("Mixture of HW %s compress and HW %s decompress with %s "
	       "at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC",
	       opts->sync_mode ? "ASYNC" : "SYNC",
	       zbuf, speed, usec, opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_dfl_perf(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len * EXPANSION_RATIO;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send2_threads(opts, &info, hw_dfl_perf);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads",
			opts->thread_num, opts->poll_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("HW %s compress with %s at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_ifl_perf(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len * EXPANSION_RATIO;
	info.out_size = opts->total_len;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send3_threads(opts, &info, hw_ifl_perf);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_send;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads",
			opts->thread_num, opts->poll_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("HW %s decompress with %s at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_dfl_perf2(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len * EXPANSION_RATIO;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send2_threads(opts, &info, hw_dfl_perf2);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads (BATCH:%d)",
			opts->thread_num, opts->poll_num, opts->batch_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("HW %s compress with %s at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

static int test_hw_ifl_perf2(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[60];
	int ret;

	info.opts = opts;
	info.in_size = opts->total_len * EXPANSION_RATIO;
	info.out_size = opts->total_len;
	info.list = get_dev_list(opts, 1);
	if (!info.list)
		return -EINVAL;
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out;
	ret = create_send3_threads(opts, &info, hw_ifl_perf2);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret)
		goto out_send;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	memset(zbuf, 0, 60);
	if (opts->sync_mode) {
		sprintf(zbuf, "%d send + %d poll threads (BATCH:%d)",
			opts->thread_num, opts->poll_num, opts->batch_num);
	} else
		sprintf(zbuf, "%d send threads", opts->thread_num);
	printf("HW %s decompress with %s at %.2fMB/s in %f usec (Bsize:%d).\n",
	       opts->sync_mode ? "ASYNC" : "SYNC", zbuf, speed, usec,
	       opts->block_size);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
out_send:
	uninit_config(&info, sched);
out:
	wd_free_list_accels(info.list);
	return ret;
}

int run_self_test(void)
{
	struct test_options opts = {
		.alg_type		= WD_ZLIB,
		.sync_mode		= 0,
		.thread_num		= 16,
		.q_num			= 16,
		.block_size		= 8192,
		.total_len		= 8192 * 10,
		.compact_run_num	= 1000,
	};
	int i, ret, f_ret = 0;

	printf("Start to run self test!\n");
	ret = test_sw_dfl_sw_ifl();
	if (ret)
		printf("Fail on running test_sw_dfl_sw_ifl():%d\n", ret);
	f_ret |= ret;
	for (i = 0; i < 10; i++) {
		switch (i) {
		case 0:
			opts.sync_mode = 0;
			opts.block_size = 8192; opts.total_len = 8192 * 10;
			break;
		case 1:
			opts.sync_mode = 1; 	opts.poll_num = 1;
			opts.block_size = 8192; opts.total_len = 8192 * 10;
			break;
		case 2:
			opts.sync_mode = 1; 	opts.poll_num = 2;
			opts.block_size = 8192; opts.total_len = 8192 * 10;
			break;
		case 3:
			opts.sync_mode = 1;	opts.poll_num = 4;
			opts.block_size = 8192;	opts.total_len = 8192 * 10;
			break;
		case 4:
			opts.sync_mode = 1; 	opts.poll_num = 8;
			opts.block_size = 8192; opts.total_len = 8192 * 10;
			break;
		case 5:
			opts.sync_mode = 0;
			opts.block_size = 1024; opts.total_len = 8192 * 10;
			break;
		case 6:
			opts.sync_mode = 1; 	opts.poll_num = 1;
			opts.block_size = 1024; opts.total_len = 8192 * 10;
			break;
		case 7:
			opts.sync_mode = 1; 	opts.poll_num = 2;
			opts.block_size = 1024; opts.total_len = 8192 * 10;
			break;
		case 8:
			opts.sync_mode = 1;	opts.poll_num = 4;
			opts.block_size = 1024;	opts.total_len = 8192 * 10;
			break;
		case 9:
			opts.sync_mode = 1; 	opts.poll_num = 8;
			opts.block_size = 1024; opts.total_len = 8192 * 10;
			break;
		default:
			return -EINVAL;
		}
		ret = test_sw_dfl_hw_ifl(&opts);
		if (ret)
			printf("Fail on test_sw_dfl_hw_ifl():%d\n", ret);
		f_ret |= ret;
		ret = test_hw_dfl_sw_ifl(&opts);
		if (ret)
			printf("Fail on test_hw_dfl_sw_ifl():%d\n", ret);
		f_ret |= ret;
		ret = test_hw_dfl_hw_ifl(&opts);
		if (ret)
			printf("Fail on test_hw_dfl_hw_ifl():%d\n", ret);
		f_ret |= ret;
		ret = test_hw_dfl_perf(&opts);
		if (ret)
			printf("Fail on test_hw_dfl_perf():%d\n", ret);
		f_ret |= ret;
		ret = test_hw_ifl_perf(&opts);
		if (ret)
			printf("Fail on test_hw_ifl_perf():%d\n", ret);
		f_ret |= ret;
	}
	printf("Start BATCH mode test for ASYNC...\n");
	for (i = 0; i < 6; i++) {
		opts.sync_mode = 1;
		/* test boundary while batch_num is 64 or 128 */
		opts.block_size = 8192;	opts.total_len = 8192 * 80;
		switch (i) {
		case 0:
			opts.batch_num = 4;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 1:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 2:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 3:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 4:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 5:
			opts.batch_num = 128;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		default:
			return -EINVAL;
		}
		ret = test_hw_dfl_perf2(&opts);
		if (ret)
			printf("Fail on test_hw_dfl_perf():%d\n", ret);
		f_ret |= ret;
		ret = test_hw_ifl_perf2(&opts);
		if (ret)
			printf("Fail on test_hw_ifl_perf():%d\n", ret);
		f_ret |= ret;
	}
	for (i = 0; i < 20; i++) {
		opts.sync_mode = 1;
		opts.block_size = 1024; opts.total_len = 8192 * 16;
		switch (i) {
		case 0:
			opts.batch_num = 4; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 1:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 2:
			opts.batch_num = 16; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 3:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 4:
			opts.batch_num = 64; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 5:
			opts.batch_num = 128;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 6:
			opts.batch_num = 4; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 7:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 8:
			opts.batch_num = 16; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 9:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 10:
			opts.batch_num = 64; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 11:
			opts.batch_num = 4;	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 12:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 13:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 14:
			opts.batch_num = 32; 	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 15:
			opts.batch_num = 4;	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 16:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 17:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 18:
			opts.batch_num = 4; 	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		case 19:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		default:
			return -EINVAL;
		}
		ret = test_hw_dfl_perf2(&opts);
		if (ret)
			printf("Fail on test_hw_dfl_perf():%d\n", ret);
		f_ret |= ret;
		usleep(10000);
		ret = test_hw_ifl_perf2(&opts);
		if (ret)
			printf("Fail on test_hw_ifl_perf():%d\n", ret);
		f_ret |= ret;
		usleep(10000);
	}
	printf("End BATCH mode test!\n");
	if (!f_ret)
		printf("Run self test successfully!\n");
	return f_ret;
}
