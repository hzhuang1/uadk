/* SPDX-License-Identifier: Apache-2.0 */
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "hisi_comp.h"
#include "wd_comp.h"

#define SYS_CLASS_DIR	"/sys/class/uacce"

/* new code */
#define WD_POOL_MAX_ENTRIES		1024
#define WD_HW_EACCESS 			62
#define MAX_RETRY_COUNTS		200000000

struct msg_pool {
	struct wd_comp_msg msg[WD_POOL_MAX_ENTRIES];
	int used[WD_POOL_MAX_ENTRIES];
	int head;
	int tail;
};

struct wd_async_msg_pool {
	struct msg_pool *pools;
	int pool_nums;
};

struct wd_comp_setting {
	struct wd_ctx_config config;
	struct wd_sched sched;
	void *sched_ctx;
	struct wd_comp_driver *driver;
	void *priv;
	struct wd_async_msg_pool pool;
} wd_comp_setting;

struct wd_comp_driver {
	const char *drv_name;
	const char *alg_name;
	__u32 drv_ctx_size;
	int (*init)(struct wd_ctx_config *config, void *priv);
	void (*exit)(void *priv);
	int (*comp_send)(handle_t ctx, struct wd_comp_msg *msg);
	int (*comp_recv)(handle_t ctx, struct wd_comp_msg *msg);
};

static struct wd_comp_driver wd_comp_driver_list[] = {
	{
		.drv_name		= "hisi_zip",
		.alg_name		= "zlib\ngzip",
		.drv_ctx_size		= sizeof(struct hisi_zip_ctx),
		.init			= hisi_zip_init,
		.exit			= hisi_zip_exit,
		.comp_send		= hisi_zip_comp_send,
		.comp_recv		= hisi_zip_comp_recv,
	},
};

static int copy_config_to_global_setting(struct wd_ctx_config *cfg)
{
	struct wd_ctx *ctxs;
	int i;

	if (cfg->ctx_num <= 0)
		return -EINVAL;

	ctxs = calloc(1, cfg->ctx_num * sizeof(struct wd_ctx));
	if (!ctxs)
		return -ENOMEM;

	for (i = 0; i < cfg->ctx_num; i++) {
		if (!cfg->ctxs[i].ctx)
			return -EINVAL;
	}

	memcpy(ctxs, cfg->ctxs, cfg->ctx_num * sizeof(struct wd_ctx));
	wd_comp_setting.config.ctxs = ctxs;
	/* Can't copy with the size of priv structure. */
	wd_comp_setting.config.priv = cfg->priv;
	wd_comp_setting.config.ctx_num = cfg->ctx_num;

	return 0;
}

static int copy_sched_to_global_setting(struct wd_sched *sched)
{
	if (!sched->name || (sched->sched_ctx_size <= 0))
		return -EINVAL;

	wd_comp_setting.sched.name = strdup(sched->name);
	wd_comp_setting.sched.sched_ctx_size = sched->sched_ctx_size;
	wd_comp_setting.sched.pick_next_ctx = sched->pick_next_ctx;
	wd_comp_setting.sched.poll_policy = sched->poll_policy;

	return 0;
}

static struct wd_comp_driver *find_comp_driver(const char *driver)
{
	const char *drv_name;
	int i, found;

	if (!driver)
		return NULL;

	/* There're no duplicated driver names in wd_comp_driver_list[]. */
	for (i = 0, found = 0; i < ARRAY_SIZE(wd_comp_driver_list); i++) {
		drv_name = wd_comp_driver_list[i].drv_name;
		if (!strncmp(driver, drv_name, strlen(driver))) {
			found = 1;
			break;
		}
	}

	if (!found)
		return NULL;

	return &wd_comp_driver_list[i];
}

static void clear_sched_in_global_setting(void)
{
	char *name = (char *)wd_comp_setting.sched.name;

	free(name);
	wd_comp_setting.sched.sched_ctx_size = 0;
	wd_comp_setting.sched.pick_next_ctx = NULL;
	wd_comp_setting.sched.poll_policy = NULL;
}

static void clear_config_in_global_setting(void)
{
	wd_comp_setting.config.priv = NULL;
	wd_comp_setting.config.ctx_num = 0;
	free(wd_comp_setting.config.ctxs);
}

/* Each context has a reqs pool. */
static int wd_init_async_request_pool(struct wd_async_msg_pool *pool)
{
	int num;

	num = wd_comp_setting.config.ctx_num;

	pool->pools = calloc(1, num * sizeof(struct msg_pool));
	if (!pool->pools)
		return -ENOMEM;

	pool->pool_nums = num;

	return 0;
}

static void wd_uninit_async_request_pool(struct wd_async_msg_pool *pool)
{
	struct msg_pool *p;
	int i, j, num;

	num = pool->pool_nums;
	for (i = 0; i < num; i++) {
		p = &pool->pools[i];
		for (j = 0; j < WD_POOL_MAX_ENTRIES; j++) {
			if (p->used[j])
				WD_ERR("Entry #%d isn't released from reqs "
					"pool.\n", j);
			memset(&p->msg[j], 0, sizeof(struct wd_comp_msg));
		}
		p->head = 0;
		p->tail = 0;
	}

	free(pool->pools);
}

/* fixme */
#if 0
static int wd_put_req_into_pool(struct wd_async_msg_pool *pool,
				handle_t h_ctx,
				struct wd_comp_req *req)
{
	struct msg_pool *p;
	int i, t, found = 0;

	for (i = 0; i < wd_comp_setting.config.ctx_num; i++) {
		if (h_ctx == wd_comp_setting.config.ctxs[i].ctx) {
			found = 1;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	p = &pool->pools[i];
	t = (p->tail + 1) % WD_POOL_MAX_ENTRIES;

	if (t == p->head)
		return -EBUSY;
	p->tail = t;

	return 0;
}
#endif

static struct wd_comp_req *wd_get_req_from_pool(struct wd_async_msg_pool *pool,
				handle_t h_ctx,
				struct wd_comp_msg *msg)
{
	struct msg_pool *p;
	struct wd_comp_msg *c_msg;
	int i, found = 0;
	int idx;

	for (i = 0; i < wd_comp_setting.config.ctx_num; i++) {
		if (h_ctx == wd_comp_setting.config.ctxs[i].ctx) {
			found = 1;
			break;
		}
	}
	if (!found)
		return NULL;

	p = &pool->pools[i];
	idx = msg->tag_id;
	c_msg = &p->msg[idx];
	memcpy(&c_msg->req, &msg->req, sizeof(struct wd_comp_req));

	return &c_msg->req;
}

static struct wd_comp_msg *wd_get_msg_from_pool(struct wd_async_msg_pool *pool,
						handle_t h_ctx,
						struct wd_comp_req *req)
{
	struct msg_pool *p;
	struct wd_comp_msg *msg;
	int i, t, found = 0;

	for (i = 0; i < wd_comp_setting.config.ctx_num; i++) {
		if (h_ctx == wd_comp_setting.config.ctxs[i].ctx) {
			found = 1;
			break;
		}
	}
	if (!found)
		return NULL;

	p = &pool->pools[i];
	if (p->head == p->tail)
		return NULL;
/*
	TODO  use bitmap to get idx for use
*/
	t = (p->tail + 1) % WD_POOL_MAX_ENTRIES;
	/* get msg from msg_pool[] */
	msg = &p->msg[p->tail];
	memcpy(&msg->req, req, sizeof(struct wd_comp_req));
	msg->tag_id = p->tail;
	p->tail = t;

	return msg;
}

int wd_comp_init(struct wd_ctx_config *config, struct wd_sched *sched)
{
	struct wd_comp_driver *driver;
	const char *driver_name;
	handle_t h_ctx;
	void *priv;
	int ret;

	/* wd_comp_init() could only be invoked once for one process. */
	if (wd_comp_setting.driver)
		return 0;

	if (!config || !sched)
		return -EINVAL;

	/* set config and sched */
	ret = copy_config_to_global_setting(config);
	if (ret < 0)
		return ret;
	ret = copy_sched_to_global_setting(sched);
	if (ret < 0)
		goto out;

	/* find driver and set driver */
	h_ctx = config->ctxs[0].ctx;
	driver_name = wd_get_driver_name(h_ctx);
	driver = find_comp_driver(driver_name);

	wd_comp_setting.driver = driver;

	/* alloc sched context memory */
	wd_comp_setting.sched_ctx = calloc(1, sched->sched_ctx_size);
	if (!wd_comp_setting.sched_ctx) {
		ret = -ENOMEM;
		goto out_sched;
	}

	/* init async request pool */
	ret = wd_init_async_request_pool(&wd_comp_setting.pool);
	if (ret < 0)
		goto out_pool;

	/* init ctx related resources in specific driver */
	priv = calloc(1, wd_comp_setting.driver->drv_ctx_size);
	if (!priv) {
		ret = -ENOMEM;
		goto out_priv;
	}
	wd_comp_setting.priv = priv;
	ret = wd_comp_setting.driver->init(&wd_comp_setting.config, priv);
	if (ret < 0)
		goto out_init;

	return 0;

out_init:
	free(priv);
out_priv:
	wd_uninit_async_request_pool(&wd_comp_setting.pool);
out_pool:
	free(wd_comp_setting.sched_ctx);
out_sched:
	clear_sched_in_global_setting();
out:
	clear_config_in_global_setting();
	return ret;
}

void wd_comp_uninit(void)
{
	void *priv;

	/* driver uninit */
	priv = wd_comp_setting.priv;
	wd_comp_setting.driver->exit(priv);
	free(priv);

	/* uninit async request pool */
	wd_uninit_async_request_pool(&wd_comp_setting.pool);

	free(wd_comp_setting.sched_ctx);

	/* unset config, sched, driver */
	wd_comp_setting.driver = NULL;
	clear_sched_in_global_setting();
	clear_config_in_global_setting();
}

__u32 wd_comp_poll_ctx(handle_t h_ctx, __u32 num)
{
	struct wd_comp_req *req;
	struct wd_comp_msg msg;

	wd_comp_setting.driver->comp_recv(h_ctx, &msg);

	req = wd_get_req_from_pool(&wd_comp_setting.pool, h_ctx, &msg);

	req->cb(0);

	/*TODO free idx of msg_pool  */

	return 0;
}

handle_t wd_comp_alloc_sess(struct wd_comp_sess_setup *setup)
{
	struct wd_comp_sess *sess;

	sess = calloc(1, sizeof(struct wd_comp_sess));
	if (!sess)
		return (handle_t)0;
	sess->alg_type = setup->alg_type;
	return (handle_t)sess;
}

void wd_comp_free_sess(handle_t h_sess)
{
}

static int fill_comp_msg(struct wd_comp_msg *msg, struct wd_comp_req *req)
{
	msg->ctx_buf = calloc(1, HW_CTX_SIZE);
	if (!msg->ctx_buf)
		return -ENOMEM;
	msg->avail_out = req->dst_len;
	msg->src = req->src;
	msg->dst = req->dst;
	msg->in_size = req->src_len;
	/* 是否尾包 1: flush end; other: sync flush */
	msg->flush_type = 1;
	/* 是否首包 1: new start; 0: old */
	msg->stream_pos = 1;
	msg->status = 0;
	return 0;
}

int wd_do_comp(handle_t h_sess, struct wd_comp_req *req)
{
	struct wd_ctx_config *config = &wd_comp_setting.config;
	void *sched_ctx = wd_comp_setting.sched_ctx;
	struct wd_comp_msg msg, resp_msg;
	struct wd_comp_sess *sess = (struct wd_comp_sess *)h_sess;
	__u64 recv_count = 0;
	handle_t h_ctx;
	int ret;

	h_ctx = wd_comp_setting.sched.pick_next_ctx(config, sched_ctx, req, 0);

	ret = fill_comp_msg(&msg, req);
	if (ret < 0)
		return ret;
	memcpy(&msg.req, req, sizeof(struct wd_comp_req));
	msg.alg_type = sess->alg_type;

	ret = wd_comp_setting.driver->comp_send(h_ctx, &msg);
	if (ret < 0) {
		WD_ERR("wd_send err!\n");
	}

	do {
		ret = wd_comp_setting.driver->comp_recv(h_ctx, &resp_msg);
		if (ret == -WD_HW_EACCESS) {
			WD_ERR("wd_recv hw err!\n");
			goto err_recv;
		} else if ((ret == -WD_EBUSY) || (ret == -EAGAIN)) {
			if (++recv_count > MAX_RETRY_COUNTS) {
				WD_ERR("wd_recv timeout fail!\n");
				ret = -ETIMEDOUT;
				goto err_recv;
			}
		}
	} while (ret < 0);

	req->src_len = resp_msg.in_cons;
	req->dst_len = resp_msg.produced;
	req->status = STATUS_OUT_DRAINED | STATUS_OUT_READY | STATUS_IN_EMPTY;
	req->flag = FLAG_INPUT_FINISH;

	return 0;
err_recv:
	free(msg.ctx_buf);
	return ret;

}

int wd_do_comp_strm(handle_t sess, struct wd_comp_req *req)
{
	struct wd_ctx_config *config = &wd_comp_setting.config;
	void *sched_ctx = wd_comp_setting.sched_ctx;
	struct wd_comp_msg msg, resp_msg;
	__u64 recv_count = 0;
	handle_t h_ctx;
	int ret;

	h_ctx = wd_comp_setting.sched.pick_next_ctx(config, sched_ctx, req, 0);

	ret = fill_comp_msg(&msg, req);
	if (ret < 0)
		return ret;
	memcpy(&msg.req, req, sizeof(struct wd_comp_req));

	/* fill trueth flag */
	msg.flush_type = req->last;

	ret = wd_comp_setting.driver->comp_send(h_ctx, &msg);
	if (ret < 0) {
		WD_ERR("wd_send err!\n");
	}

	do {
		ret = wd_comp_setting.driver->comp_recv(h_ctx, &resp_msg);
		if (ret == -WD_HW_EACCESS) {
			WD_ERR("wd_recv hw err!\n");
			goto err_recv;
		} else if (ret == -WD_EBUSY) {
			if (++recv_count > MAX_RETRY_COUNTS) {
				WD_ERR("wd_recv timeout fail!\n");
				ret = -ETIMEDOUT;
				goto err_recv;
			}
		}
	} while (ret < 0);

	req->src_len = resp_msg.req.src_len;
	req->dst_len = resp_msg.req.dst_len;
	req->status = resp_msg.req.status;

	free(msg.ctx_buf);
	return 0;
err_recv:
	free(msg.ctx_buf);
	return ret;
}

int wd_do_comp_async(handle_t h_sess, struct wd_comp_req *req)
{
	struct wd_ctx_config *config = &wd_comp_setting.config;
	void *sched_ctx = wd_comp_setting.sched_ctx;
	struct wd_comp_msg *msg;
	handle_t h_ctx;

	h_ctx = wd_comp_setting.sched.pick_next_ctx(config, sched_ctx, req, 0);

	msg = wd_get_msg_from_pool(&wd_comp_setting.pool, h_ctx, req);

	wd_comp_setting.driver->comp_send(h_ctx, msg);

	return 0;

}

int wd_comp_poll(__u32 *count)
{
	struct wd_ctx_config *config = &wd_comp_setting.config;
	void *sched_ctx = wd_comp_setting.sched_ctx;

	wd_comp_setting.sched.poll_policy(config, sched_ctx);

	return 0;
}
