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
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
	__u32 tmp_sz, tout_sz;

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
		if (opts->is_stream) {
			tmp_sz = tbuf_sz;
			ret = hw_stream_compress(opts->alg_type,
						 opts->block_size,
						 opts->data_fmt, tbuf, &tmp_sz,
						 tdata->src, tdata->src_sz);
			if (ret) {
				printf("Fail to deflate by zlib: %d\n", ret);
				goto out_run;
			}
			tout_sz = tdata->dst_sz;
			ret = hw_stream_decompress(opts->alg_type,
						   opts->block_size,
						   opts->data_fmt, tdata->dst,
						   &tout_sz, tbuf, tmp_sz);
			if (ret) {
				printf("Fail to inflate by zlib: %d\n", ret);
				goto out_run;
			}
		} else {
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
			tout_sz = tbuf_sz / EXPANSION_RATIO;
		}
		ret = calculate_md5(&final_md5, tdata->dst, (size_t)tout_sz);
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
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
		/* hw_deflate2() equals to hw_deflate3() */
		ret = hw_deflate3(h_dfl, tdata->src, tdata->dst, tdata->src_sz,
				  tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_dfl);
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
		/* hw_inflate2() equals to hw_inflate3() */
		ret = hw_inflate3(h_ifl, tdata->src, tdata->dst, tdata->src_sz,
				  tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_ifl);
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		free(tdata->dst);
	else
		info->out_size = tdata->dst_sz;
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
	info.in_buf = malloc(info.in_size);
	if (!info.in_buf)
		return -ENOMEM;
	gen_random_data(info.in_buf, info.in_size);
	ret = create_send2_threads(&opts, &info, sw_dfl_sw_ifl);
	if (ret)
		goto out;
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(&opts, &info);
	if (ret)
		goto out;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts.total_len * opts.thread_num * opts.compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	printf("Mixature of SW compress and SW decompress with %d threads "
	       "at %.2fMB/s in %f usec.\n", opts.thread_num, speed, usec);
	free_threads(&info);
	return 0;
out:
	free(info.in_buf);
	return ret;
}

int test_hw(struct test_options *opts, char *model)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	struct wd_sched *sched = NULL;
	double ilen, usec, speed;
	char zbuf[120];
	int ret, zbuf_idx, ifl_flag = 0;
	void *(*func)(void *);
	size_t tbuf_sz;
	void *tbuf = NULL;
	ssize_t file_sz = 0;

	if (!opts || !model) {
		ret = -EINVAL;
		goto out;
	}
	info.opts = opts;
	memset(zbuf, 0, 120);
	if (!strcmp(model, "sw_dfl_hw_ifl")) {
		func = sw_dfl_hw_ifl;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len;
		zbuf_idx = sprintf(zbuf, "Mix SW deflate and HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_sw_ifl")) {
		func = hw_dfl_sw_ifl;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len;
		zbuf_idx = sprintf(zbuf, "Mix HW %s %s deflate and SW inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_hw_ifl")) {
		func = hw_dfl_hw_ifl;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len + 4096;
		zbuf_idx = sprintf(zbuf,
				   "Mix HW %s %s deflate and HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_perf")) {
		func = hw_dfl_perf;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len * EXPANSION_RATIO;
		zbuf_idx = sprintf(zbuf, "HW %s %s deflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_perf2")) {
		func = hw_dfl_perf2;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len * EXPANSION_RATIO;
		zbuf_idx = sprintf(zbuf, "HW %s %s deflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_ifl_perf")) {
		func = hw_ifl_perf;
		info.in_size = opts->total_len * EXPANSION_RATIO;
		info.out_size = opts->total_len;
		zbuf_idx = sprintf(zbuf, "HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
		ifl_flag = 1;
	} else if (!strcmp(model, "hw_ifl_perf2")) {
		func = hw_ifl_perf2;
		info.in_size = opts->total_len * EXPANSION_RATIO;
		info.out_size = opts->total_len;
		zbuf_idx = sprintf(zbuf, "HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
		ifl_flag = 1;
	} else {
		printf("Wrong model is specified:%s\n", model);
		ret = -EINVAL;
		goto out;
	}

	info.list = get_dev_list(opts, 1);
	if (!info.list) {
		ret = -EINVAL;
		goto out;
	}
	ret = init_ctx_config(opts, &info, &sched);
	if (ret)
		goto out_cfg;
	info.out_buf = malloc(info.out_size);
	if (!info.out_buf) {
		ret = -ENOMEM;
		goto out_dst;
	}
	if (ifl_flag) {
		tbuf_sz = opts->total_len;
		tbuf = malloc(tbuf_sz);
		if (!tbuf) {
			ret = -ENOMEM;
			goto out_buf;
		}
		gen_random_data(tbuf, tbuf_sz);
		info.in_buf = malloc(info.in_size);
		if (!info.in_buf) {
			ret = -ENOMEM;
			goto out_src;
		}
		ret = sw_deflate(tbuf, info.in_buf, tbuf_sz, opts);
		if (ret)
			goto out_dfl;
	} else {
		info.in_buf = malloc(info.in_size);
		if (!info.in_buf) {
			ret = -ENOMEM;
			goto out_src;
		}
		gen_random_data(info.in_buf, info.in_size);
	}
	ret = create_send2_threads(opts, &info, func);
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
	if (opts->is_file && opts->fd_out) {
		/* write output from thread 0 to file */
		file_sz = write(opts->fd_out, info.out_buf, info.out_size);
		if (file_sz < info.out_size) {
			printf("Expect to write %ld bytes. "
			       "But only write %ld bytes!\n",
			       info.out_size, file_sz);
			return -EIO;
		}
	}

	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	if (opts->sync_mode) {
		zbuf_idx += sprintf(zbuf + zbuf_idx,
				    " with %d send + %d poll threads",
				    opts->thread_num,
				    opts->poll_num);
	} else {
		zbuf_idx += sprintf(zbuf + zbuf_idx,
				    " with %d send threads",
				    opts->thread_num);
	}
	printf("%s at %.2fMB/s in %f usec (Bsize:%d).\n",
	       zbuf, speed, usec, opts->block_size);
	free(info.out_buf);
	uninit_config(&info, sched);
	free_threads(&info);
	return 0;
out_poll:
	free_threads(&info);
	free(info.out_buf);
out_send:
out_dfl:
	free(info.in_buf);
out_src:
	if (ifl_flag && tbuf)
		free(tbuf);
out_buf:
	free(info.out_buf);
out_dst:
	uninit_config(&info, sched);
out_cfg:
	wd_free_list_accels(info.list);
out:
	printf("Fail to run %s() (%d)!\n", model, ret);
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
	for (i = 0; i < 1; i++) {
		opts.sync_mode = 0;
		opts.is_stream = 1;
		f_ret |= test_hw(&opts, "hw_dfl_hw_ifl");
		f_ret |= test_hw(&opts, "hw_dfl_perf");
		f_ret |= test_hw(&opts, "hw_ifl_perf");
	}
	opts.is_stream = 0;	/* restore to BLOCK mode */
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
		f_ret |= test_hw(&opts, "sw_dfl_hw_ifl");
		f_ret |= test_hw(&opts, "hw_dfl_sw_ifl");
		f_ret |= test_hw(&opts, "hw_dfl_hw_ifl");
		f_ret |= test_hw(&opts, "hw_dfl_perf");
		f_ret |= test_hw(&opts, "hw_ifl_perf");
	}
	printf("Start BATCH mode test for ASYNC...\n");
	for (i = 0; i < 5; i++) {
		opts.sync_mode = 1;
		/* test boundary while batch_num is 64 or 128 */
		opts.block_size = 8192;	opts.total_len = 8192 * 80;
		switch (i) {
		case 0:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 1:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 2:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 3:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 4:
			opts.batch_num = 128;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		default:
			return -EINVAL;
		}
		f_ret |= test_hw(&opts, "hw_dfl_perf2");
		f_ret |= test_hw(&opts, "hw_ifl_perf2");
	}
	for (i = 0; i < 25; i++) {
		opts.sync_mode = 1;
		opts.block_size = 1024; opts.total_len = 8192 * 16;
		switch (i) {
		case 0:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 1:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 2:
			opts.batch_num = 32; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 3:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 4:
			opts.batch_num = 128; 	opts.poll_num = 1;
			opts.thread_num = 1;
			break;
		case 5:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 6:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 7:
			opts.batch_num = 32; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 8:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 9:
			opts.batch_num = 128; 	opts.poll_num = 1;
			opts.thread_num = 2;
			break;
		case 10:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 11:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 12:
			opts.batch_num = 32; 	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 13:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 14:
			opts.batch_num = 128; 	opts.poll_num = 1;
			opts.thread_num = 4;
			break;
		case 15:
			opts.batch_num = 8;	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 16:
			opts.batch_num = 16; 	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 17:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 18:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 19:
			opts.batch_num = 128; 	opts.poll_num = 1;
			opts.thread_num = 8;
			break;
		case 20:
			opts.batch_num = 8; 	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		case 21:
			opts.batch_num = 16;	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		case 22:
			opts.batch_num = 32;	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		case 23:
			opts.batch_num = 64;	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		case 24:
			opts.batch_num = 128; 	opts.poll_num = 1;
			opts.thread_num = 16;
			break;
		default:
			return -EINVAL;
		}
		f_ret |= test_hw(&opts, "hw_dfl_perf2");
		usleep(10000);
		f_ret |= test_hw(&opts, "hw_ifl_perf2");
		usleep(10000);
	}
	printf("End BATCH mode test!\n");
	if (!f_ret)
		printf("Run self test successfully!\n");
	return f_ret;
}

static int set_default_opts(struct test_options *opts)
{
	struct stat statbuf;
	int ret;

	if (!opts->block_size)
		opts->block_size = 8192;
	if (opts->is_file) {
		ret = fstat(opts->fd_in, &statbuf);
		if (!ret)
			opts->total_len = statbuf.st_size;
	}
	if (!opts->total_len) {
		if (opts->block_size)
			opts->total_len = opts->block_size * 10;
		else
			opts->total_len = 8192 * 10;
	}
	if (!opts->thread_num)
		opts->thread_num = 1;
	if (!opts->q_num)
		opts->q_num = opts->thread_num;
	if (!opts->compact_run_num)
		opts->compact_run_num = 1;
	if (!opts->poll_num)
		opts->poll_num = 1;
	return 0;
}

int run_cmd(struct test_options *opts)
{
	int ret;

	set_default_opts(opts);
	if (opts->op_type == WD_DIR_COMPRESS) {
		if (opts->verify)
			ret = test_hw(opts, "hw_dfl_sw_ifl");
		else
			ret = test_hw(opts, "hw_dfl_perf");
	} else {
		if (opts->verify)
			ret = test_hw(opts, "sw_dfl_hw_ifl");
		else
			ret = test_hw(opts, "hw_ifl_perf");
	}
	return ret;
}
