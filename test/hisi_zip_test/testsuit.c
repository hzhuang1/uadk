// SPDX-License-Identifier: Apache-2.0

#include "test_lib.h"

/* PADDING could avoid blocking in HW inflation */
#define HIZIP_PADDING	4

static void *sw_dfl_sw_ifl(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	void *tbuf;
	size_t tbuf_sz;
	chunk_list_t *tlist;
	comp_md5_t final_md5 = {0};
	int i, ret;

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = mmap_alloc(tbuf_sz);
	if (!tbuf)
		return (void *)(uintptr_t)(-ENOMEM);
	info->in_chunk_sz = opts->block_size;
	info->out_chunk_sz = opts->block_size;
	tlist = create_chunk_list(tbuf, tbuf_sz,
				  info->in_chunk_sz * EXPANSION_RATIO);
	if (!tlist) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < opts->compact_run_num; i++) {
		/*
		 * tdata->out_list/tlist would be updated by
		 * sw_deflate2()/sw_inflate2().
		 * So reset it for each iteration.
		 */
		init_chunk_list(tlist, tbuf, tbuf_sz,
			       	info->in_chunk_sz * EXPANSION_RATIO);
		init_chunk_list(tdata->out_list, tdata->dst, tdata->dst_sz,
				info->out_chunk_sz);
		ret = sw_deflate2(tdata->in_list, tlist, opts);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out_dfl;
		}
		ret = sw_inflate2(tlist, tdata->out_list, opts);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out_dfl;
		}
		ret = calculate_md5(&final_md5, tdata->dst, tdata->dst_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
			goto out_dfl;
		}
		ret = cmp_md5(&tdata->md5, &final_md5);
		if (ret) {
			printf("MD5 is unmatched (%d) at %dth times on "
				"thread %d\n", ret, i, tdata->tid);
			goto out_dfl;
		}
	}
	free_chunk_list(tlist);
	mmap_free(tbuf, tbuf_sz);
	return NULL;
out_dfl:
	free_chunk_list(tlist);
out:
	mmap_free(tbuf, tbuf_sz);
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
	chunk_list_t *tlist;
	comp_md5_t final_md5 = {0};
	int i, ret;
	__u32 tout_sz;

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = mmap_alloc(tbuf_sz);
	if (!tbuf)
		return (void *)(uintptr_t)(-ENOMEM);
	tlist = create_chunk_list(tbuf, tbuf_sz,
				  info->in_chunk_sz * EXPANSION_RATIO);
	if (!tlist) {
		ret = -ENOMEM;
		goto out;
	}
	if (opts->is_stream) {
		/* STREAM mode: only one entry in the list */
		init_chunk_list(tdata->in_list, tdata->src,
				tdata->src_sz, tdata->src_sz);
		for (i = 0; i < opts->compact_run_num; i++) {
			init_chunk_list(tlist, tbuf, tbuf_sz, tbuf_sz);
			init_chunk_list(tdata->out_list, tdata->dst,
					tdata->dst_sz, tdata->dst_sz);
			ret = sw_deflate2(tdata->in_list, tlist, opts);
			if (ret) {
				printf("Fail to deflate by zlib: %d\n", ret);
				goto out_strm;
			}
			tout_sz = tdata->out_list->size + HIZIP_PADDING;
			ret = hw_stream_decompress(opts->alg_type,
						   opts->block_size,
						   opts->data_fmt,
						   tdata->dst,
						   &tout_sz,
						   tlist->addr,
						   tlist->size);
			if (ret) {
				printf("Fail to inflate by HW: %d\n", ret);
				goto out_strm;
			}
			ret = calculate_md5(&tdata->md5, tdata->in_list->addr,
					    tdata->in_list->size);
			if (ret) {
				printf("Fail to generate MD5 (%d)\n", ret);
				goto out_strm;
			}
			ret = calculate_md5(&final_md5, tdata->out_list->addr,
					    tout_sz);
			if (ret) {
				printf("Fail to generate MD5 (%d)\n", ret);
				goto out_strm;
			}
			ret = cmp_md5(&tdata->md5, &final_md5);
			if (ret) {
				printf("MD5 is unmatched (%d) at %dth times on "
					"thread %d\n", ret, i, tdata->tid);
				goto out_strm;
			}
		}
		free_chunk_list(tlist);
		mmap_free(tbuf, tbuf_sz);
		return NULL;
	}

	/* BLOCK mode */
        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl) {
		ret = -EINVAL;
		goto out_strm;
	}

	init_chunk_list(tdata->in_list, tdata->src, tdata->src_sz,
			info->in_chunk_sz);
	for (i = 0; i < opts->compact_run_num; i++) {
		init_chunk_list(tlist, tbuf, tbuf_sz,
			       	info->in_chunk_sz * EXPANSION_RATIO);
		init_chunk_list(tdata->out_list, tdata->dst, tdata->dst_sz,
				info->out_chunk_sz);
		ret = sw_deflate2(tdata->in_list, tlist, opts);
		if (ret) {
			printf("Fail to deflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = hw_inflate4(h_ifl, tlist, tdata->out_list, opts,
				  &tdata->sem);
		if (ret) {
			printf("Fail to inflate by HW: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&tdata->md5, tdata->src, tdata->src_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
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
	wd_comp_free_sess(h_ifl);
	free_chunk_list(tlist);
	mmap_free(tbuf, tbuf_sz);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	wd_comp_free_sess(h_ifl);
out_strm:
	free_chunk_list(tlist);
out:
	mmap_free(tbuf, tbuf_sz);
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
	chunk_list_t *tlist;
	comp_md5_t final_md5 = {0};
	int i, ret;
	__u32 tmp_sz;

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = mmap_alloc(tbuf_sz);
	if (!tbuf)
		return (void *)(uintptr_t)(-ENOMEM);
	tlist = create_chunk_list(tbuf, tbuf_sz,
				  opts->block_size * EXPANSION_RATIO);
	if (!tlist) {
		ret = -ENOMEM;
		goto out;
	}
	if (opts->is_stream) {
		/* STREAM mode: only one entry in the list */
		init_chunk_list(tdata->in_list, tdata->src,
				tdata->src_sz, tdata->src_sz);
		for (i = 0; i < opts->compact_run_num; i++) {
			init_chunk_list(tlist, tbuf, tbuf_sz, tbuf_sz);
			init_chunk_list(tdata->out_list, tdata->dst,
					tdata->dst_sz, tdata->dst_sz);
			tmp_sz = tbuf_sz;
			ret = hw_stream_compress(opts->alg_type,
						 opts->block_size,
						 opts->data_fmt,
						 tlist->addr,
						 &tmp_sz,
						 tdata->src,
						 tdata->src_sz);
			if (ret) {
				printf("Fail to deflate by HW: %d\n", ret);
				goto out_strm;
			}
			tlist->size = tmp_sz;	// write back
			ret = sw_inflate2(tlist, tdata->out_list, opts);
			if (ret) {
				printf("Fail to inflate by zlib: %d\n", ret);
				goto out_strm;
			}
			ret = calculate_md5(&tdata->md5, tdata->in_list->addr,
					    tdata->in_list->size);
			if (ret) {
				printf("Fail to generate MD5 (%d)\n", ret);
				goto out_strm;
			}
			ret = calculate_md5(&final_md5, tdata->out_list->addr,
					    tdata->out_list->size);
			if (ret) {
				printf("Fail to generate MD5 (%d)\n", ret);
				goto out_strm;
			}
			ret = cmp_md5(&tdata->md5, &final_md5);
			if (ret) {
				printf("MD5 is unmatched (%d) at %dth times on "
					"thread %d\n", ret, i, tdata->tid);
				goto out_strm;
			}
		}
		free_chunk_list(tlist);
		mmap_free(tbuf, tbuf_sz);
		return NULL;
	}

	/* BLOCK mode */
        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl) {
		ret = -EINVAL;
		goto out_strm;
	}

	init_chunk_list(tdata->in_list, tdata->src, tdata->src_sz,
			info->in_chunk_sz);
	for (i = 0; i < opts->compact_run_num; i++) {
		init_chunk_list(tlist, tbuf, tbuf_sz,
			       	opts->block_size * EXPANSION_RATIO);
		init_chunk_list(tdata->out_list, tdata->dst, tdata->dst_sz,
				info->out_chunk_sz);
		ret = hw_deflate4(h_dfl, tdata->in_list, tlist, opts,
				  &tdata->sem);
		if (ret) {
			printf("Fail to deflate by HW: %d\n", ret);
			goto out_run;
		}
		ret = sw_inflate2(tlist, tdata->out_list, opts);
		if (ret) {
			printf("Fail to inflate by zlib: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&tdata->md5, tdata->src, tdata->src_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
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
	free_chunk_list(tlist);
	mmap_free(tbuf, tbuf_sz);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	wd_comp_free_sess(h_dfl);
out_strm:
	free_chunk_list(tlist);
out:
	mmap_free(tbuf, tbuf_sz);
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
	chunk_list_t *tlist;
	comp_md5_t final_md5 = {0};
	int i, ret;
	__u32 tmp_sz, tout_sz;

	tbuf_sz = tdata->src_sz * EXPANSION_RATIO;
	tbuf = mmap_alloc(tbuf_sz);
	if (!tbuf)
		return (void *)(uintptr_t)(-ENOMEM);
	if (opts->is_stream) {
		for (i = 0; i < opts->compact_run_num; i++) {
			tmp_sz = tbuf_sz;
			ret = hw_stream_compress(opts->alg_type,
						 opts->block_size,
						 opts->data_fmt,
						 tbuf,
						 &tmp_sz,
						 tdata->src,
						 tdata->src_sz);
			if (ret) {
				printf("Fail to deflate by HW: %d\n", ret);
				goto out;
			}
			tout_sz = tdata->dst_sz + HIZIP_PADDING;
			ret = hw_stream_decompress(opts->alg_type,
						   opts->block_size,
						   opts->data_fmt,
						   tdata->dst,
						   &tout_sz,
						   tbuf,
						   tmp_sz);
			if (ret) {
				printf("Fail to inflate by HW: %d\n", ret);
				goto out;
			}
			ret = calculate_md5(&tdata->md5, tdata->in_list->addr,
					    tdata->in_list->size);
			if (ret) {
				printf("Fail to generate MD5 (%d)\n", ret);
				goto out;
			}
			ret = calculate_md5(&final_md5, tdata->dst, tout_sz);
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
		mmap_free(tbuf, tbuf_sz);
		return NULL;
	}

	/* BLOCK mode */
	tlist = create_chunk_list(tbuf, tbuf_sz,
				  opts->block_size * EXPANSION_RATIO);
	if (!tlist) {
		ret = -ENOMEM;
		goto out;
	}
        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl) {
		ret = -EINVAL;
		goto out_dfl;
	}

	setup.op_type = WD_DIR_DECOMPRESS;
	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl) {
		ret = -EINVAL;
		goto out_ifl;
	}

	for (i = 0; i < opts->compact_run_num; i++) {
		init_chunk_list(tlist, tbuf, tbuf_sz,
			       	opts->block_size * EXPANSION_RATIO);
		init_chunk_list(tdata->out_list, tdata->dst,
				tdata->dst_sz,
				info->out_chunk_sz);
		ret = hw_deflate4(h_dfl, tdata->in_list, tlist, opts,
				  &tdata->sem);
		if (ret) {
			printf("Fail to deflate by HW: %d\n", ret);
			goto out_run;
		}
		ret = hw_inflate4(h_ifl, tlist, tdata->out_list, opts,
				  &tdata->sem);
		if (ret) {
			printf("Fail to inflate by HW: %d\n", ret);
			goto out_run;
		}
		ret = calculate_md5(&tdata->md5, tdata->src, tdata->src_sz);
		if (ret) {
			printf("Fail to generate MD5 (%d)\n", ret);
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
	wd_comp_free_sess(h_ifl);
	wd_comp_free_sess(h_dfl);
	free_chunk_list(tlist);
	mmap_free(tbuf, tbuf_sz);
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out_run:
	wd_comp_free_sess(h_ifl);
out_ifl:
	wd_comp_free_sess(h_dfl);
out_dfl:
	free_chunk_list(tlist);
out:
	mmap_free(tbuf, tbuf_sz);
	return (void *)(uintptr_t)(ret);
}

static void *hw_dfl_perf(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	//chunk_list_t *list, *p = NULL;
	handle_t h_dfl;
	int i, ret;
	//size_t out_sz = tdata->dst_sz, file_sz = 0;
	uint32_t tout_sz;

	if (opts->is_stream) {
		for (i = 0; i < opts->compact_run_num; i++) {
			tout_sz = tdata->dst_sz;
			ret = hw_stream_compress(opts->alg_type,
						 opts->block_size,
						 opts->data_fmt,
						 tdata->dst,
						 &tout_sz,
						 tdata->src,
						 tdata->src_sz);
			if (ret) {
				printf("Fail to deflate by HW: %d\n", ret);
				return (void *)(uintptr_t)ret;
			}
			ret = tout_sz;
		}
		return NULL;
	}

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		init_chunk_list(tdata->out_list, tdata->dst,
				tdata->dst_sz,
				info->out_chunk_sz);
		ret = hw_deflate4(h_dfl, tdata->in_list, tdata->out_list, opts,
				  &tdata->sem);
		if (ret) {
			printf("Fail to deflate by HW: %d\n", ret);
			goto out;
		}
	}
	wd_comp_free_sess(h_dfl);
#if 0
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
	else {
		/* put in in hw_dfl_perf() temporarily */
		if (opts->is_file && opts->fd_out && opts->is_stream) {
			file_sz = write(opts->fd_out, tdata->dst, out_sz);
			if (file_sz < out_sz) {
				printf("Expect to write %ld bytes. "
				       "But only write %ld bytes!\n",
				       out_sz, file_sz);
				goto out_wrt;
			}
		} else if (opts->is_file && opts->fd_out) {
			p = list;
			/* write output from thread 0 to file */
			for (i = 0; i < HIZIP_CHUNK_LIST_ENTRIES; i++) {
				file_sz = write(opts->fd_out, p->addr, p->size);
				if (file_sz < p->size) {
					printf("Expect to write %ld bytes. "
					       "But only write %ld bytes!\n",
					       p->size, file_sz);
					goto out_wrt;
				}
				p = p->next;
				if (!p->next)
					break;
			}
		}
		info->total_out = out_sz;
	}
#endif
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	wd_comp_free_sess(h_dfl);
	return (void *)(uintptr_t)(ret);
}

static void *hw_ifl_perf(void *arg)
{
	thread_data_t *tdata = (thread_data_t *)arg;
	struct hizip_test_info *info = tdata->info;
	struct test_options *opts = info->opts;
	struct wd_comp_sess_setup setup = {0};
	//chunk_list_t *list, *p = NULL;
	handle_t h_ifl;
	int i, ret;
	//size_t out_sz = tdata->dst_sz, file_sz = 0;
	uint32_t tout_sz;

	fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
	if (opts->is_stream) {
		for (i = 0; i < opts->compact_run_num; i++) {
			init_chunk_list(tdata->out_list, tdata->dst,
					tdata->dst_sz,
					info->out_chunk_sz);
			tout_sz = tdata->dst_sz + HIZIP_PADDING;
			ret = hw_stream_decompress(opts->alg_type,
						   opts->block_size,
						   opts->data_fmt,
						   tdata->dst,
						   &tout_sz,
						   tdata->src,
						   tdata->src_sz);
			if (ret) {
				printf("Fail to inflate by HW: %d\n", ret);
				return (void *)(uintptr_t)ret;
			}
			ret = tout_sz;
		}
		return NULL;
	}

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
		ret = hw_inflate4(h_ifl, tdata->in_list, tdata->out_list, opts,
				  &tdata->sem);
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
		if (ret) {
			printf("Fail to inflate by HW: %d\n", ret);
			goto out;
		}
	}
	wd_comp_free_sess(h_ifl);
#if 0
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
	else {
		/* put in in hw_ifl_perf() temporarily */
		if (opts->is_file && opts->fd_out && opts->is_stream) {
			file_sz = write(opts->fd_out, tdata->dst, out_sz);
			if (file_sz < out_sz) {
				printf("Expect to write %ld bytes. "
				       "But only write %ld bytes!\n",
				       out_sz, file_sz);
				goto out_wrt;
			}
		} else if (opts->is_file && opts->fd_out) {
			p = list;
			/* write output from thread 0 to file */
			for (i = 0; i < HIZIP_CHUNK_LIST_ENTRIES; i++) {
				file_sz = write(opts->fd_out, p->addr, p->size);
				if (file_sz < p->size) {
					printf("Expect to write %ld bytes. "
					       "But only write %ld bytes!\n",
					       p->size, file_sz);
					goto out_wrt;
				}
				p = p->next;
				if (!p->next)
					break;
			}
		}
		info->total_out = out_sz;
	}
#endif
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
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
	size_t out_sz = 0;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;

	h_dfl = wd_comp_alloc_sess(&setup);
	if (!h_dfl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		/* hw_deflate2() equals to hw_deflate3() */
		ret = hw_deflate3(h_dfl, tdata->src, tdata->dst, tdata->src_sz,
				  &out_sz, tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_dfl);
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
	else
		info->total_out = out_sz;
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
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
	size_t out_sz = 0;

        setup.alg_type = opts->alg_type;
        setup.mode = opts->sync_mode ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_DECOMPRESS;

	h_ifl = wd_comp_alloc_sess(&setup);
	if (!h_ifl)
		return (void *)(uintptr_t)(-EINVAL);

	for (i = 0; i < opts->compact_run_num; i++) {
		/* hw_inflate2() equals to hw_inflate3() */
		ret = hw_inflate3(h_ifl, tdata->src, tdata->dst, tdata->src_sz,
				  &out_sz, tdata);
		if (ret)
			goto out;
	}
	wd_comp_free_sess(h_ifl);
	/* Thread 0 shares output buf with info->out_buf. */
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
	else
		info->total_out = out_sz;
	/* mark sending thread to end */
	__atomic_add_fetch(&sum_thread_end, 1, __ATOMIC_ACQ_REL);
	return NULL;
out:
	if (tdata->tid)
		mmap_free(tdata->dst, tdata->dst_sz);
	wd_comp_free_sess(h_ifl);
	return (void *)(uintptr_t)(ret);
}

/* Only support SYNC mode */
int test_sw_dfl_sw_ifl(struct test_options *opts)
{
	struct hizip_test_info info = {0};
	struct timeval start_tvl, end_tvl;
	double ilen, usec, speed;
	int ret;

	info.opts = opts;
	info.in_chunk_sz = opts->block_size;
	info.out_chunk_sz = opts->block_size;
	info.in_size = opts->total_len;
	info.out_size = opts->total_len;
	info.in_buf = mmap_alloc(info.in_size);
	if (!info.in_buf) {
		ret = -ENOMEM;
		goto out;
	}
	gen_random_data(info.in_buf, info.in_size);
	ret = create_send3_threads(opts, &info, sw_dfl_sw_ifl);
	if (ret) {
		mmap_free(info.in_buf, info.in_size);
		goto out;
	}
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	if (ret) {
		free_threads(&info);
		goto out;
	}
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = opts->total_len * opts->thread_num * opts->compact_run_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	printf("Mixature of SW compress and SW decompress with %d threads "
	       "at %.2fMB/s in %f usec.\n", opts->thread_num, speed, usec);
	free_threads(&info);
	return 0;
out:
	printf("Fail to run %s (%d)\n", __func__, ret);
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
	size_t tbuf_sz = 0, /*out_sz = 0, */ifl_in_sz = 0;
	void *tbuf = NULL;
	ssize_t file_sz;
	struct stat statbuf;
	chunk_list_t *tlist;

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
		info.in_chunk_sz = opts->block_size;
		info.out_chunk_sz = opts->block_size;
		zbuf_idx = sprintf(zbuf, "Mix SW deflate and HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_sw_ifl")) {
		func = hw_dfl_sw_ifl;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len;
		info.in_chunk_sz = opts->block_size;
		info.out_chunk_sz = opts->block_size;
		zbuf_idx = sprintf(zbuf, "Mix HW %s %s deflate and SW inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
	} else if (!strcmp(model, "hw_dfl_hw_ifl")) {
		func = hw_dfl_hw_ifl;
		info.in_size = opts->total_len;
		info.out_size = opts->total_len;
		info.in_chunk_sz = opts->block_size;
		info.out_chunk_sz = opts->block_size;
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
		info.in_chunk_sz = opts->block_size;
		info.out_chunk_sz = opts->block_size * EXPANSION_RATIO;
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
		info.in_size = opts->total_len;
		info.out_size = opts->total_len * INFLATION_RATIO;
		info.in_chunk_sz = opts->block_size;
		info.out_chunk_sz = opts->block_size * INFLATION_RATIO;
		zbuf_idx = sprintf(zbuf, "HW %s %s inflate",
				   opts->sync_mode ? "ASYNC" : "SYNC",
				   opts->is_stream ? "STREAM" : "BLOCK");
		ifl_flag = 1;
		ifl_in_sz = info.in_size;
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
	if (opts->is_file) {
		ret = fstat(opts->fd_in, &statbuf);
		if (!ret) {
			opts->total_len = statbuf.st_size;
			info.in_size = opts->total_len;
			if (ifl_flag) {
				info.out_size = ALIGN(opts->total_len,
						      opts->block_size);
				info.out_size *= INFLATION_RATIO;
			} else {
				info.out_size = opts->total_len *
						EXPANSION_RATIO;
			}
		}
	}
	info.in_buf = mmap_alloc(info.in_size);
	if (!info.in_buf) {
		ret = -ENOMEM;
		goto out_src;
	}
	ret = create_send3_threads(opts, &info, func);
	if (ret)
		goto out_send;
	ret = create_poll2_threads(opts, &info, poll2_thread_func,
				   opts->poll_num);
	if (ret)
		goto out_poll;
	if (opts->is_file) {
		file_sz = read(opts->fd_in, info.in_buf, info.in_size);
		if (file_sz < info.in_size) {
			printf("Expect to read %ld bytes. "
			       "But only read %ld bytes!\n",
			       info.in_size, file_sz);
			goto out_buf;
		}
	} else {
		if (ifl_flag) {
			thread_data_t *tdata = info.tdatas;
			tbuf_sz = info.in_size / EXPANSION_RATIO;
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
			tbuf = mmap_alloc(tbuf_sz);
			if (!tbuf) {
				ret = -ENOMEM;
				goto out_buf;
			}
			tlist = create_chunk_list(tbuf, tbuf_sz,
						  opts->block_size /
						  EXPANSION_RATIO);
			init_chunk_list(tlist, tbuf, tbuf_sz,
					opts->block_size / EXPANSION_RATIO);
			init_chunk_list(tdata[0].in_list, tdata[0].src,
					tdata[0].src_sz,
					info.in_chunk_sz);
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
			gen_random_data(tbuf, tbuf_sz);
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
			ret = sw_deflate2(tlist, tdata[0].in_list, opts);
		fprintf(stderr, "#%s, %d, ret:%d\n", __func__, __LINE__, ret);
			if (ret)
				goto out_dfl;
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
			mmap_free(tbuf, tbuf_sz);
			//info.in_size = out_sz;
		fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
		} else
			gen_random_data(info.in_buf, info.in_size);
	}
	gettimeofday(&start_tvl, NULL);
	ret = attach_threads(opts, &info);
	fprintf(stderr, "#%s, %d, ret:%d\n", __func__, __LINE__, ret);
	if (ret)
		goto out_poll;
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
#if 0
	if (opts->is_file && opts->fd_out) {
		/* write output from thread 0 to file */
		file_sz = write(opts->fd_out, info.out_buf, info.total_out);
		if (file_sz < info.out_size) {
			printf("Expect to write %ld bytes. "
			       "But only write %ld bytes!\n",
			       info.out_size, file_sz);
			goto out_poll;
		}
	}
#endif

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
	if (ifl_in_sz)
		info.in_size = ifl_in_sz;
	fprintf(stderr, "#%s, %d\n", __func__, __LINE__);
	uninit_config(&info, sched);
	free_threads(&info);
	usleep(1000);
	return 0;
out_poll:
	free_threads(&info);
out_send:
out_dfl:
	if (ifl_flag && tbuf && tbuf_sz)
		mmap_free(tbuf, tbuf_sz);
out_buf:
	if (ifl_in_sz)
		info.in_size = ifl_in_sz;
out_src:
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
		.thread_num		= 1,
		//.thread_num		= 16,
		.q_num			= 16,
		//.block_size		= 1024,
		//.total_len		= 1024 * 2,
		.block_size		= 8192,
		.total_len		= 8192 * 10,
		//.compact_run_num	= 1000,
		.compact_run_num	= 1,
	};
	int /*i, */f_ret = 0;

	printf("Start to run self test!\n");
	f_ret |= test_sw_dfl_sw_ifl(&opts);
	opts.is_stream = 0;
	f_ret |= test_hw(&opts, "sw_dfl_hw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_sw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_hw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_perf");
	f_ret |= test_hw(&opts, "hw_ifl_perf");
	opts.is_stream = 1;
	f_ret |= test_hw(&opts, "sw_dfl_hw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_sw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_hw_ifl");
	f_ret |= test_hw(&opts, "hw_dfl_perf");
	f_ret |= test_hw(&opts, "hw_ifl_perf");
#if 0
	for (i = 0; i < 1; i++) {
		opts.sync_mode = 0;
		opts.is_stream = 1;
		//f_ret |= test_hw(&opts, "hw_dfl_hw_ifl");
		//f_ret |= test_hw(&opts, "hw_dfl_perf");
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
	return 0;
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
#endif
	if (!f_ret)
		printf("Run self test successfully!\n");
	return f_ret;
}

static int set_default_opts(struct test_options *opts)
{
	if (!opts->block_size)
		opts->block_size = 8192;
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
	if (opts->alg_type == WD_COMP_ALG_MAX)
		opts->alg_type = WD_GZIP;
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
