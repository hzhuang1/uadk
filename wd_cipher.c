/* SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <pthread.h>
#include "wd_cipher.h"
#include "include/drv/wd_cipher_drv.h"
#include "wd_util.h"

#define XTS_MODE_KEY_DIVISOR	2
#define SM4_KEY_SIZE		16
#define DES_KEY_SIZE		8
#define DES3_2KEY_SIZE		(2 * DES_KEY_SIZE)
#define DES3_3KEY_SIZE		(3 * DES_KEY_SIZE)
#define MAX_CIPHER_KEY_SIZE	64

#define WD_POOL_MAX_ENTRIES	1024
#define DES_WEAK_KEY_NUM	4
#define MAX_RETRY_COUNTS	200000000


static __u64 des_weak_key[DES_WEAK_KEY_NUM] = {
	0x0101010101010101, 0xFEFEFEFEFEFEFEFE,
	0xE0E0E0E0F1F1F1F1, 0x1F1F1F1F0E0E0E0E
};

struct wd_cipher_setting {
	struct wd_ctx_config_internal config;
	struct wd_sched      sched;
	void *sched_ctx;
	struct wd_cipher_driver *driver;
	void *priv;
	struct wd_async_msg_pool pool;
};

static struct wd_cipher_setting wd_cipher_setting;
extern struct wd_cipher_driver wd_cipher_hisi_cipher_driver;

#ifdef WD_STATIC_DRV
static void wd_cipher_set_static_drv(void)
{
	/*
	 * a parameter can be introduced to decide to choose
	 * specific driver. Same as dynamic case.
	 */
	wd_cipher_setting.driver = &wd_cipher_hisi_cipher_driver;
}
#else
static void __attribute__((constructor)) wd_cipher_open_driver(void)
{
	void *driver;

	/* vendor driver should be put in /usr/lib/wd/ */
	driver = dlopen("libhisi_sec.so", RTLD_NOW);
	if (!driver)
		WD_ERR("fail to open libhisi_sec.so\n");
}
#endif

void wd_cipher_set_driver(struct wd_cipher_driver *drv)
{
	wd_cipher_setting.driver = drv;
}

static int is_des_weak_key(const __u64 *key)
{
	int i;

	for (i = 0; i < DES_WEAK_KEY_NUM; i++) {
		if (*key == des_weak_key[i])
			return 1;
	}

	return 0;
}

static int aes_key_len_check(__u16 length)
{
	switch (length) {
		case AES_KEYSIZE_128:
		case AES_KEYSIZE_192:
		case AES_KEYSIZE_256:
			return 0;
		default:
			return -EINVAL;
	}
}

static int cipher_key_len_check(enum wd_cipher_alg alg, __u16 length)
{
	int ret = 0;

	switch (alg) {
	case WD_CIPHER_SM4:
		if (length != SM4_KEY_SIZE)
			ret = -EINVAL;
		break;
	case WD_CIPHER_AES:
		ret = aes_key_len_check(length);
		break;
	case WD_CIPHER_DES:
		if (length != DES_KEY_SIZE)
			ret = -EINVAL;
		break;
	case WD_CIPHER_3DES:
		if (length != DES3_2KEY_SIZE && length != DES3_3KEY_SIZE)
			ret = -EINVAL;
		break;
	default:
		WD_ERR("%s: input alg err!\n", __func__);
		return -EINVAL;
	}

	return ret;
}

int wd_cipher_set_key(handle_t h_sess, const __u8 *key, __u32 key_len)
{
	struct wd_cipher_sess *sess = (struct wd_cipher_sess *)h_sess;
	__u16 length = key_len;
	int ret;

	if (!key || !sess || !sess->key) {
		WD_ERR("cipher set key inpupt param err!\n");
		return -EINVAL;
	}

	if (sess->mode == WD_CIPHER_XTS)
		length = key_len / XTS_MODE_KEY_DIVISOR;

	ret = cipher_key_len_check(sess->alg, length);
	if (ret) {
		WD_ERR("cipher set key inpupt key length err!\n");
		return -EINVAL;
	}
	if (sess->alg == WD_CIPHER_DES && is_des_weak_key((__u64 *)key)) {
		WD_ERR("input des key is weak key!\n");
		return -EINVAL;
	}

	sess->key_bytes = key_len;
	memcpy(sess->key, key, key_len);

	return 0;
}

handle_t wd_cipher_alloc_sess(struct wd_cipher_sess_setup *setup)
{
	struct wd_cipher_sess *sess = NULL;

	if (!setup) {
		WD_ERR("cipher input setup is NULL!\n");
		return (handle_t)0;
	}

	sess = malloc(sizeof(struct wd_cipher_sess));
	if (!sess) {
		WD_ERR("fail to alloc session memory!\n");
		return (handle_t)0;
	}
	memset(sess, 0, sizeof(struct wd_cipher_sess));
	sess->alg = setup->alg;
	sess->mode = setup->mode;
	sess->key = malloc(MAX_CIPHER_KEY_SIZE);
	if (!sess->key) {
		WD_ERR("fail to alloc key memory!\n");
		free(sess);
		return (handle_t)0;
	}
	memset(sess->key, 0, MAX_CIPHER_KEY_SIZE);

	return (handle_t)sess;
}

void wd_cipher_free_sess(handle_t h_sess)
{
	if (!h_sess) {
		WD_ERR("cipher input h_sess is NULL!\n");
		return;
	}
	struct wd_cipher_sess *sess = (struct wd_cipher_sess *)h_sess;

	if (sess->key) {
		wd_memset_zero(sess->key, MAX_CIPHER_KEY_SIZE);
		free(sess->key);
	}
	free(sess);
}

int wd_cipher_init(struct wd_ctx_config *config, struct wd_sched *sched)
{
	void *priv;
	int ret;

	if (wd_cipher_setting.config.ctx_num) {
		WD_ERR("Cipher have initialized.\n");
		return 0;
	}
	if (!config || !sched) {
		WD_ERR("wd cipher config or sched is NULL!\n");
		return -EINVAL;
	}

	if (!wd_is_sva(config->ctxs[0].ctx)) {
		WD_ERR("err, non sva, please check system!\n");
		return -EINVAL;
	}

	ret = wd_init_ctx_config(&wd_cipher_setting.config, config);
	if (ret < 0) {
		WD_ERR("failed to set config, ret = %d!\n", ret);
		return ret;
	}

	ret = wd_init_sched(&wd_cipher_setting.sched, sched);
	if (ret < 0) {
		WD_ERR("failed to set sched, ret = %d!\n", ret);
		goto out;
	}

#ifdef WD_STATIC_DRV
	/* set driver */
	wd_cipher_set_static_drv();
#endif

	/* sadly find we allocate async pool for every ctx */
	ret = wd_init_async_request_pool(&wd_cipher_setting.pool,
					 config->ctx_num, WD_POOL_MAX_ENTRIES,
					 sizeof(struct wd_cipher_msg));
	if (ret < 0) {
		WD_ERR("failed to init req pool, ret = %d!\n", ret);
		goto out_sched;
	}

	/* init ctx related resources in specific driver */
	priv = calloc(1, wd_cipher_setting.driver->drv_ctx_size);
	if (!priv) {
		ret = -ENOMEM;
		goto out_priv;
	}
	wd_cipher_setting.priv = priv;
	/* sec init */
	ret = wd_cipher_setting.driver->init(&wd_cipher_setting.config, priv);
	if (ret < 0) {
		WD_ERR("hisi sec init failed.\n");
		goto out_init;
	}

	return 0;

out_init:
	free(priv);
out_priv:
	wd_uninit_async_request_pool(&wd_cipher_setting.pool);
out_sched:
	wd_clear_sched(&wd_cipher_setting.sched);
out:
	wd_clear_ctx_config(&wd_cipher_setting.config);
	return ret;
}

void wd_cipher_uninit(void)
{
	void *priv = wd_cipher_setting.priv;

	if (!priv) {
		wd_cipher_setting.driver->exit(priv);
		wd_cipher_setting.priv = NULL;
		free(priv);
	}

	wd_uninit_async_request_pool(&wd_cipher_setting.pool);

	wd_clear_sched(&wd_cipher_setting.sched);
	wd_clear_ctx_config(&wd_cipher_setting.config);
}

static void fill_request_msg(struct wd_cipher_msg *msg,
			     struct wd_cipher_req *req,
			     struct wd_cipher_sess *sess)
{
	memcpy(&msg->req, req, sizeof(struct wd_cipher_req));

	msg->alg = sess->alg;
	msg->mode = sess->mode;
	msg->op_type = req->op_type;
	msg->in = req->src;
	msg->in_bytes = req->in_bytes;
	msg->out = req->dst;
	msg->out_bytes = req->out_bytes;
	msg->key = sess->key;
	msg->key_bytes = sess->key_bytes;
	msg->iv = req->iv;
	msg->iv_bytes = req->iv_bytes;
}

int wd_do_cipher_sync(handle_t h_sess, struct wd_cipher_req *req)
{
	struct wd_ctx_config_internal *config = &wd_cipher_setting.config;
	struct wd_cipher_sess *sess = (struct wd_cipher_sess *)h_sess;
	struct wd_ctx_internal *ctx;
	struct wd_cipher_msg msg;
	__u64 recv_cnt = 0;
	int index, ret;

	if (unlikely(!sess || !req)) {
		WD_ERR("cipher input sess or req is NULL.\n");
		return -EINVAL;
	}

	if (req->out_buf_bytes < req->in_bytes) {
		WD_ERR("cipher set out_buf_bytes is error!\n");
		return -EINVAL;
	}

	index = wd_cipher_setting.sched.pick_next_ctx(0, req, NULL);
	if (index >= config->ctx_num) {
		WD_ERR("fail to pick a proper ctx!\n");
		return -EINVAL;
	}
	ctx = config->ctxs + index;

	memset(&msg, 0, sizeof(struct wd_cipher_msg));
	fill_request_msg(&msg, req, sess);
	req->state = 0;

	pthread_mutex_lock(&ctx->lock);

	ret = wd_cipher_setting.driver->cipher_send(ctx->ctx, &msg);
	if (ret < 0) {
		pthread_mutex_unlock(&ctx->lock);
		WD_ERR("wd cipher send err!\n");
		return ret;
	}

	do {
		ret = wd_cipher_setting.driver->cipher_recv(ctx->ctx, &msg);
		if (ret == -WD_HW_EACCESS) {
			WD_ERR("wd cipher recv err!\n");
			goto recv_err;
		} else if (ret == -EAGAIN) {
			if (++recv_cnt > MAX_RETRY_COUNTS) {
				WD_ERR("wd cipher recv timeout fail!\n");
				ret = -ETIMEDOUT;
				goto recv_err;
			}
		}
	} while (ret < 0);
	pthread_mutex_unlock(&ctx->lock);

	return 0;
recv_err:
	req->state = msg.result;
	pthread_mutex_unlock(&ctx->lock);
	return ret;
}

int wd_do_cipher_async(handle_t h_sess, struct wd_cipher_req *req)
{
	struct wd_ctx_config_internal *config = &wd_cipher_setting.config;
	struct wd_cipher_sess *sess = (struct wd_cipher_sess *)h_sess;
	struct wd_ctx_internal *ctx;
	struct wd_cipher_msg *msg;
	int index, idx, ret;

	if (unlikely(!sess || !req || !req->cb)) {
		WD_ERR("cipher input sess or req is NULL.\n");
		return -EINVAL;
	}

	if (req->out_buf_bytes < req->in_bytes) {
		WD_ERR("cipher set out_buf_bytes is error!\n");
		return -EINVAL;
	}

	index = wd_cipher_setting.sched.pick_next_ctx(0, req, NULL);
	if (unlikely(index >= config->ctx_num)) {
		WD_ERR("fail to pick a proper ctx!\n");
		return -EINVAL;
	}
	ctx = config->ctxs + index;

	idx = wd_get_msg_from_pool(&wd_cipher_setting.pool, index,
				   (void **)&msg);
	if (idx < 0) {
		WD_ERR("busy, failed to get msg from pool!\n");
		return -EBUSY;
	}

	fill_request_msg(msg, req, sess);
	msg->tag = idx;

	pthread_mutex_lock(&ctx->lock);

	ret = wd_cipher_setting.driver->cipher_send(ctx->ctx, msg);
	if (ret < 0) {
		if (ret != -EBUSY)
			WD_ERR("wd cipher async send err!\n");
		wd_put_msg_to_pool(&wd_cipher_setting.pool, index, msg->tag);
	}
	pthread_mutex_unlock(&ctx->lock);

	return ret;
}

int wd_cipher_poll_ctx(__u32 index, __u32 expt, __u32* count)
{
	struct wd_ctx_config_internal *config = &wd_cipher_setting.config;
	struct wd_ctx_internal *ctx = config->ctxs + index;
	struct wd_cipher_msg resp_msg, *msg;
	struct wd_cipher_req *req;
	__u64 recv_count = 0;
	int ret;

	if (unlikely(index >= config->ctx_num || !count)) {
		WD_ERR("wd cipher poll ctx input param is NULL!\n");
		return -EINVAL;
	}

	do {
		pthread_mutex_lock(&ctx->lock);
		ret = wd_cipher_setting.driver->cipher_recv(ctx->ctx,
							      &resp_msg);
		pthread_mutex_unlock(&ctx->lock);
		if (ret == -EAGAIN) {
			break;
		} else if (ret < 0) {
			WD_ERR("wd cipher recv hw err!\n");
			break;
		}
		recv_count++;
		msg = wd_find_msg_in_pool(&wd_cipher_setting.pool, index,
					  resp_msg.tag);
		if (!msg) {
			WD_ERR("failed to get msg from pool!\n");
			break;
		}

		msg->tag = resp_msg.tag;
		req = &msg->req;

		req->cb(req, req->cb_param);
		/* free msg cache to msg_pool */
		wd_put_msg_to_pool(&wd_cipher_setting.pool, index,
				   resp_msg.tag);
	} while (--expt);
	*count = recv_count;

	return ret;
}

int wd_cipher_poll(__u32 expt, __u32 *count)
{
	int ret;

	ret = wd_cipher_setting.sched.poll_policy(0, 0, expt, count);
	if (ret < 0)
		return ret;

	return 0;
}
