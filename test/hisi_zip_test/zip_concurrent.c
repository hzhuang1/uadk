#include <openssl/md5.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>

#include "sched_sample.h"
#include "wd_comp.h"

#define SEND_THREAD_ID		0x55000000
#define POLL_THREAD_ID		0x33000000

#define MAX_BLOCK_BUF_SIZE	(8 << 20)

typedef struct _comp_mode_t {
	unsigned int	repeat_num;
	size_t		buf_len;	/* src buffer len */
	unsigned	async : 1;
	unsigned	inflate : 1;
	unsigned	stream : 1;
	unsigned	type : 2;	/* algorithm type: "zlib", "gzip" */
} comp_mode_t;

typedef struct _comp_md5_t {
	MD5_CTX		md5_ctx;
	unsigned char	md[MD5_DIGEST_LENGTH];
} comp_md5_t;

typedef struct _thread_data_t {
	unsigned int	tid;
	comp_mode_t	mode;
	void		*src;
	void		*dst;
	size_t		src_len;
	size_t		dst_len;
	comp_md5_t	md5;		/* original md5 */
	wd_alg_comp_cb_t	*cb;
} thread_data_t;

typedef struct _thread_info_t {
	pthread_t	*send_tds;	/* send thread array */
	int		send_tnum;	/* send thread number */
	thread_data_t	*send_tdata;
	pthread_t	*poll_tds;	/* poll thread array */
	int		poll_tnum;	/* poll thread number */
	thread_data_t	*poll_tdata;
} thread_info_t;

void gen_random_data(void *buf, size_t len)
{
	int i;
	uint32_t seed = 0;
	unsigned short rand_state[3] = {(seed >> 16) & 0xffff, seed & 0xffff,
					0x330e};

	for (i = 0; i < len >> 3; i++)
		*((uint64_t *)buf + i) = nrand48(rand_state);
}

int calculate_md5(comp_md5_t *md5, const void *buf, size_t len)
{
	if (!md5 || !buf || !len)
		return -EINVAL;
	MD5_Init(&md5->md5_ctx);
	MD5_Update(&md5->md5_ctx, buf, len);
	MD5_Final(md5->md, &md5->md5_ctx);
	return 0;
}

void dump_md5(comp_md5_t *md5)
{
	int i;

	for (i = 0; i < MD5_DIGEST_LENGTH - 1; i++)
		printf("%02x-", md5->md[i]);
	printf("%02x\n", md5->md[i]);
}

int cmp_md5(comp_md5_t *orig, comp_md5_t *final)
{
	int i;

	if (!orig || !final)
		return -EINVAL;
	for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
		if (orig->md[i] != final->md[i]) {
			printf("Original MD5: ");
			dump_md5(orig);
			printf("Final MD5: ");
			dump_md5(final);
			return -EINVAL;
		}
	}
	return 0;
}

static int create_send_threads(thread_info_t *info,
				void *(*send_thread_func)(void *arg),
				comp_mode_t mode,
				int num)
{
	void *src;
	pthread_t *tds;
	thread_data_t *tdatas;
	int i, ret;

	tds = calloc(1, sizeof(pthread_t) * num);
	if (!tds)
		return -ENOMEM;
	tdatas = calloc(1, sizeof(thread_data_t) * num);
	if (!tdatas) {
		ret = -ENOMEM;
		goto out;
	}
	/* 1 src buffer : N dst buffer */
	src = malloc(mode.buf_len);
	if (!src) {
		ret = -ENOMEM;
		goto out_src;
	}
	for (i = 0; i < num; i++) {
		tdatas[i].tid = SEND_THREAD_ID | i;
		memcpy(&tdatas[i].mode, &mode, sizeof(comp_mode_t));
		tdatas[i].src_len = mode.buf_len;
		tdatas[i].src = src;
		if (mode.stream)
			tdatas[i].dst_len = tdatas[i].src_len << 4;
		else
			tdatas[i].dst_len = MAX_BLOCK_BUF_SIZE;
		tdatas[i].dst = malloc(tdatas[i].dst_len);
		if (!tdatas[i].dst) {
			ret = -ENOMEM;
			goto out_dst;
		}
		gen_random_data(tdatas[i].src, tdatas[i].src_len);
		calculate_md5(&tdatas[i].md5, tdatas[i].src, tdatas[i].src_len);
	}
	for (i = 0; i < num; i++) {
		ret = pthread_create(&tds[i], NULL, send_thread_func,
				     &tdatas[i]);
		if (ret) {
			printf("Fail to create threads %d\n", i);
			goto out_tds;
		}
	}
	info->send_tds = tds;
	info->send_tnum = num;
	info->send_tdata = tdatas;
	return 0;

out_tds:
	for (; i > 0;)
		pthread_cancel(tds[--i]);
out_dst:
	for (; i > 0;) {
		i--;
		free(tdatas[i].dst);
	}
	free(tdatas[0].src);
out_src:
	free(tdatas);
out:
	free(tds);
	return ret;
}

static void free_threads(thread_info_t *info)
{
	int i;

	if (info->send_tds) {
		for (i = 0; i < info->send_tnum; i++)
			free(info->send_tdata[i].dst);
		free(info->send_tdata[0].src);
		free(info->send_tdata);
		free(info->send_tds);
	}
}

static int create_poll_threads(thread_info_t *info, int num)
{
	return 0;
}

void attach_all_threads(thread_info_t *info)
{
	int i;

	if (info->poll_tds) {
		for (i = 0; i < info->poll_tnum; i++)
			pthread_join(info->poll_tds[i], NULL);
	}
	if (info->send_tds) {
		for (i = 0; i < info->send_tnum; i++)
			pthread_join(info->send_tds[i], NULL);
	}
}

/* measure performance */
void *send_thread_func1(void *arg)
{
	struct wd_comp_req req;
	struct wd_comp_sess_setup setup;
	thread_data_t *td = (thread_data_t *)arg;
	handle_t h_sess;
	int i, ret;

	memset(&setup, 0, sizeof(struct wd_comp_sess_setup));
        setup.alg_type = td->mode.type;
        setup.mode = td->mode.async ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = td->mode.inflate ? WD_DIR_DECOMPRESS : WD_DIR_COMPRESS;
        h_sess = wd_comp_alloc_sess(&setup);
	if (!h_sess) {
		printf("Fail to allocate session for compresion algorithm!\n");
		return NULL;
	}

	req.src = td->src;
	req.src_len = td->src_len;
	req.dst = td->dst;
	req.dst_len = td->dst_len;
	req.op_type = td->mode.inflate ? WD_DIR_DECOMPRESS : WD_DIR_COMPRESS;
	req.cb = NULL;

	for (i = 0; i < td->mode.repeat_num; i++) {
		req.src_len = td->src_len;
		req.dst_len = td->dst_len;
		if (td->mode.async) {
			ret = wd_do_comp_async(h_sess, &req);
		} else {
			if (td->mode.stream)
				ret = wd_do_comp_strm(h_sess, &req);
			else
				ret = wd_do_comp_sync(h_sess, &req);
		}
		if (ret) {
			printf("Fail to execute comp operation!\n");
			break;
		}
	}
	wd_comp_free_sess(h_sess);
	return NULL;
}

/* compress & decomrpess. Use MD5 to verify it. */
void *send_thread_func2(void *arg)
{
	struct wd_comp_req req;
	struct wd_comp_sess_setup setup;
	thread_data_t *td = (thread_data_t *)arg;
	size_t tmp_len, tmp_src_len;
	comp_md5_t final_md5;
	void *tmp;
	handle_t h_comp, h_decomp;
	int i, ret;

	if (td->mode.stream)
		tmp_len = (td->dst_len > td->src_len) ? td->dst_len :
			td->src_len;
	else
		tmp_len = MAX_BLOCK_BUF_SIZE;
	tmp = malloc(tmp_len);
	if (!tmp) {
		ret = -ENOMEM;
		goto out;
	}
	memset(&setup, 0, sizeof(struct wd_comp_sess_setup));
        setup.alg_type = td->mode.type;
        setup.mode = td->mode.async ? CTX_MODE_ASYNC : CTX_MODE_SYNC;
        setup.op_type = WD_DIR_COMPRESS;
	h_comp = wd_comp_alloc_sess(&setup);
	if (!h_comp) {
		printf("Fail to allocate session for compresion algorithm!\n");
		ret = -EINVAL;
		goto out_alloc_comp;
	}

        setup.op_type = WD_DIR_DECOMPRESS;
	h_decomp = wd_comp_alloc_sess(&setup);
	if (!h_decomp) {
		printf("Fail to allocate session for decompresion algorithm!\n");
		ret = -EINVAL;
		goto out_alloc_decomp;
	}

	for (i = 0; i < td->mode.repeat_num; i++) {
		memset(&req, 0, sizeof(struct wd_comp_req));
		req.src = td->src;
		req.src_len = td->src_len;
		req.dst = td->dst;
		req.dst_len = td->dst_len;
		req.op_type = WD_DIR_COMPRESS;
		req.cb = NULL;

		if (td->mode.async) {
			ret = wd_do_comp_async(h_comp, &req);
		} else {
			if (td->mode.stream)
				ret = wd_do_comp_strm(h_comp, &req);
			else
				ret = wd_do_comp_sync(h_comp, &req);
		}
		if (ret) {
			printf("Fail to execute comp operation!\n");
			goto out_comp;
		}
		/* update the buf size with hardware generated buf size */
		tmp_src_len = req.dst_len;

		memset(&req, 0, sizeof(struct wd_comp_req));
		req.src = td->dst;
		req.src_len = tmp_src_len;
		req.dst = tmp;
		req.dst_len = tmp_len;
		req.op_type = WD_DIR_DECOMPRESS;
		req.cb = NULL;

		if (td->mode.async) {
			ret = wd_do_comp_async(h_decomp, &req);
		} else {
			if (td->mode.stream)
				ret = wd_do_comp_strm(h_decomp, &req);
			else
				ret = wd_do_comp_sync(h_decomp, &req);
		}
		if (ret) {
			printf("Fail to execute comp operation!\n");
			goto out_comp;
		}
		ret = calculate_md5(&final_md5, req.dst, req.dst_len);
		if (ret) {
			printf("Fail to calculate MD5 on output buffer (%d)\n",
				ret);
			goto out_comp;
		}
		ret = cmp_md5(&td->md5, &final_md5);
		if (ret)
			printf("Fail at %dth times!\n", i + 1);
	}
	wd_comp_free_sess(h_decomp);
	wd_comp_free_sess(h_comp);
	free(tmp);
	return NULL;
out_comp:
	wd_comp_free_sess(h_decomp);
out_alloc_decomp:
	wd_comp_free_sess(h_comp);
out_alloc_comp:
	free(tmp);
out:
	printf("Error (%d)\n", ret);
	return NULL;
}

void *async_cb(void *arg)
{
	return NULL;
}

int create_ctxs(comp_mode_t mode, int ctx_num,
		struct wd_ctx_config *cfg,
		struct wd_sched **sched)
{
	struct uacce_dev_list *list;
	int ret = 0;
	int i;

	if (!cfg)
		return -EINVAL;
	list = wd_get_accel_list("zlib");
	if (!list) {
		printf("Fail to get zlib device\n");
		return -ENODEV;
	}
	memset(cfg, 0, sizeof(struct wd_ctx_config));
	/*
	 * ctx_num is binded to one operation.
	 * COMPRESS and DECOMPRESS are different operations. So the demanded
	 * context numbers need to be doubled.
	 */
	cfg->ctx_num = ctx_num << 1;
	cfg->ctxs = calloc(1, sizeof(struct wd_ctx) * cfg->ctx_num);
	if (!cfg->ctxs)
		goto out;

	for (i = 0; i < cfg->ctx_num; i++) {
		cfg->ctxs[i].ctx = wd_request_ctx(list->dev);
		cfg->ctxs[i].ctx_mode = mode.async ? CTX_MODE_ASYNC :
					CTX_MODE_SYNC;
	}

	*sched = sample_sched_alloc(SCHED_POLICY_RR, 2, MAX_NUMA_NUM,
				    wd_comp_poll_ctx);
	if (!*sched) {
		printf("Fail to alloc sched!\n");
		goto out_sched;
	}
	(*sched)->name = strdup("sched_rr");

	/* If there is no numa, we defualt config to zero */
	if (list->dev->numa_id < 0)
		list->dev->numa_id = 0;

	for (i = 0; i < ctx_num; i++)
		cfg->ctxs[i].op_type = WD_DIR_COMPRESS;
	ret = sample_sched_fill_data(*sched, list->dev->numa_id,
				     0, 0, 0, ctx_num - 1);
	if (ret) {
		printf("Fail to fill sched data!\n");
		goto out_fill;
	}
	for (i = ctx_num; i < (ctx_num << 1); i++)
		cfg->ctxs[i].op_type = WD_DIR_DECOMPRESS;
	ret = sample_sched_fill_data(*sched, list->dev->numa_id,
				     0, 1, ctx_num, (ctx_num << 1) - 1);
	if (ret) {
		printf("Fail to fill sched data!\n");
		goto out_fill;
	}

	ret = wd_comp_init(cfg, *sched);
	if (ret) {
		printf("Fail to comp ctx!\n");
		goto out_fill;
	}

	wd_free_list_accels(list);
	return 0;

out_fill:
	sample_sched_release(*sched);
out_sched:
	for (i = 0; i < cfg->ctx_num; i++)
		wd_release_ctx(cfg->ctxs[i].ctx);
	free(cfg->ctxs);
out:
	wd_free_list_accels(list);

	return ret;
}

void free_ctxs(struct wd_ctx_config *cfg, struct wd_sched *sched)
{
	int i;

	wd_comp_uninit();
	for (i = 0; i < cfg->ctx_num; i++)
		wd_release_ctx(cfg->ctxs[i].ctx);
	free(cfg->ctxs);
	sample_sched_release(sched);
}

int concurrent_model1(comp_mode_t mode, int s_num, int p_num)
{
	thread_info_t info;
	struct timeval start_tvl, end_tvl;
	struct wd_ctx_config cfg;
	struct wd_sched *sched;
	int ret;
	double ilen, usec, speed;

	ret = create_ctxs(mode, s_num, &cfg, &sched);
	if (ret)
		return ret;
	memset(&info, 0, sizeof(thread_info_t));
	ret = create_send_threads(&info, send_thread_func1, mode, s_num);
	if (ret)
		goto out;
	ret = create_poll_threads(&info, p_num);
	if (ret)
		goto out_thrd;
	gettimeofday(&start_tvl, NULL);
	attach_all_threads(&info);
	gettimeofday(&end_tvl, NULL);
	timersub(&end_tvl, &start_tvl, &start_tvl);
	usec = (double)(start_tvl.tv_sec * 1000000 + start_tvl.tv_usec);
	ilen = mode.buf_len * s_num * mode.repeat_num;
	speed = ilen * 1000 * 1000 / 1024 / 1024 / usec;
	fprintf(stderr, "%s with %d threads at %.2fMB/s.\n",
		mode.inflate ? "Decompress" : "Compress",
		s_num, speed);
	fprintf(stderr, "Cost time:%f usec\n", usec);

	free_threads(&info);
	free_ctxs(&cfg, sched);
	return 0;

out_thrd:
	free_threads(&info);
out:
	free_ctxs(&cfg, sched);
	return ret;
}

int concurrent_model2(comp_mode_t mode, int s_num, int p_num)
{
	thread_info_t info;
	struct wd_ctx_config cfg;
	struct wd_sched *sched;
	int ret;

	ret = create_ctxs(mode, s_num, &cfg, &sched);
	if (ret)
		return ret;
	memset(&info, 0, sizeof(thread_info_t));
	ret = create_send_threads(&info, send_thread_func2, mode, s_num);
	if (ret)
		goto out;
	ret = create_poll_threads(&info, p_num);
	if (ret)
		goto out_thrd;
	attach_all_threads(&info);
	free_threads(&info);
	free_ctxs(&cfg, sched);
	printf("%s: finished\n", __func__);
	return 0;

out_thrd:
	free_threads(&info);
out:
	free_ctxs(&cfg, sched);
	return ret;
}

int main(int argc, char **argv)
{
	comp_mode_t mode;
	int ret;

#if 1
	mode.repeat_num = 1000000;
	//mode.buf_len = 1 << 19;
	mode.buf_len = 8 << 10;
	mode.async = 0;
	mode.stream = 0;
	mode.inflate = 0;
	mode.type = WD_ZLIB;
	ret = concurrent_model1(mode, 32, 0);
	if (ret)
		printf("Fail to run concurrent_model1 (%d)\n", ret);
	mode.repeat_num = 100000;
	//mode.buf_len = 1 << 19;
	ret = concurrent_model2(mode, 32, 0);
	if (ret)
		printf("Fail to run concurrent_model2 (%d)\n", ret);
#else
	mode.repeat_num = 1000000;
	mode.buf_len = 8 << 10;
	mode.async = 0;
	mode.stream = 0;
	mode.inflate = 0;
	mode.type = WD_ZLIB;
	ret = concurrent_model1(mode, 1, 0);
	if (ret)
		printf("Fail to run concurrent_model1 (%d)\n", ret);
	ret = concurrent_model2(mode, 1, 0);
	if (ret)
		printf("Fail to run concurrent_model2 (%d)\n", ret);
#endif
	return ret;
}
