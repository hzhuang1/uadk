/* SPDX-License-Identifier: Apache-2.0 */

#include <asm/types.h>
#include "drv/wd_comp_drv.h"
#include "hisi_qm_udrv.h"
#include "smm.h"
#include "wd.h"
#include "wd_sched.h"

#define	ZLIB		0
#define	GZIP		1

#define DEFLATE		0
#define INFLATE		1

#define ASIZE		(2 * 512 * 1024)
#define HW_CTX_SIZE	(64*1024)

enum alg_type {
	HW_ZLIB  = 0x02,
	HW_GZIP,
};

enum hw_state {
	HZ_STATELESS,
	HZ_STATEFUL,
};

enum hw_flush {
	HZ_SYNC_FLUSH,
	HZ_FINISH,
};

enum hw_stream_status {
	HZ_STREAM_OLD,
	HZ_STREAM_NEW,
};

struct hisi_zip_sqe {
	__u32 consumed;
	__u32 produced;
	__u32 comp_data_length;
	__u32 dw3;
	__u32 input_data_length;
	__u32 lba_l;
	__u32 lba_h;
	__u32 dw7;
	__u32 dw8;
	__u32 dw9;
	__u32 dw10;
	__u32 priv_info;
	__u32 dw12;
	__u32 tag;
	__u32 dest_avail_out;
	__u32 ctx_dw0;
	__u32 comp_head_addr_l;
	__u32 comp_head_addr_h;
	__u32 source_addr_l;
	__u32 source_addr_h;
	__u32 dest_addr_l;
	__u32 dest_addr_h;
	__u32 stream_ctx_addr_l;
	__u32 stream_ctx_addr_h;
	__u32 cipher_key1_addr_l;
	__u32 cipher_key1_addr_h;
	__u32 cipher_key2_addr_l;
	__u32 cipher_key2_addr_h;
	__u32 ctx_dw1;
	__u32 ctx_dw2;
	__u32 isize;
	__u32 checksum;

};

#define BLOCK_SIZE	(1 << 19)
#define CACHE_NUM	1	//4

#define ZLIB_HEADER	"\x78\x9c"
#define ZLIB_HEADER_SZ	2

/*
 * We use a extra field for gzip block length. So the fourth byte is \x04.
 * This is necessary because our software don't know the size of block when
 * using an hardware decompresser (It is known by hardware). This help our
 * decompresser to work and helpfully, compatible with gzip.
 */
#define GZIP_HEADER	"\x1f\x8b\x08\x04\x00\x00\x00\x00\x00\x03"
#define GZIP_HEADER_SZ	10
#define GZIP_EXTRA_SZ	10
#define GZIP_TAIL_SZ	8

#define BLOCK_MIN		(1 << 10)
#define BLOCK_MIN_MASK		0x3FF
#define BLOCK_MAX		(1 << 20)
#define BLOCK_MAX_MASK		0xFFFFF
#define STREAM_MIN		(1 << 10)
#define STREAM_MIN_MASK		0x3FF
#define STREAM_MAX		(1 << 20)
#define STREAM_MAX_MASK		0xFFFFF

#define HISI_SCHED_INPUT	0
#define HISI_SCHED_OUTPUT	1

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_ERRNO		(-1)
#define Z_STREAM_ERROR	(-EIO)

#define swab32(x) \
	((((x) & 0x000000ff) << 24) | \
	(((x) & 0x0000ff00) <<  8) | \
	(((x) & 0x00ff0000) >>  8) | \
	(((x) & 0xff000000) >> 24))

#define cpu_to_be32(x) swab32(x)

#define STREAM_FLUSH_SHIFT 25
#define MIN_AVAILOUT_SIZE 4096
#define STREAM_POS_SHIFT 2
#define STREAM_MODE_SHIFT 1

#define HZ_NEGACOMPRESS 0x0d
#define HZ_CRC_ERR 0x10
#define HZ_DECOMP_END 0x13

#define HZ_CTX_ST_MASK 0x000f
#define HZ_LSTBLK_MASK 0x0100
#define HZ_STATUS_MASK 0xff
#define HZ_REQ_TYPE_MASK 0xff

#define HZ_HADDR_SHIFT		32

#define lower_32_bits(addr) ((__u32)((__u64)(addr)))
#define upper_32_bits(addr) ((__u32)((__u64)(addr) >> HZ_HADDR_SHIFT))

struct hisi_zip_ctx {
	struct wd_ctx_config	config;
};

static int hisi_zip_init(struct wd_ctx_config *config, void *priv)
{
	struct hisi_qm_priv qm_priv;
	struct hisi_zip_ctx *zip_ctx = (struct hisi_zip_ctx *)priv;
	handle_t h_ctx, h_qp;
	int i, j, ret = 0;

	/* allocate qp for each context */
	for (i = 0; i < config->ctx_num; i++) {
		h_ctx = config->ctxs[i].ctx;
		qm_priv.sqe_size = sizeof(struct hisi_zip_sqe);
		qm_priv.op_type = config->ctxs[i].op_type;
		h_qp = hisi_qm_alloc_qp(&qm_priv, h_ctx);
		if (!h_qp) {
			ret = -EINVAL;
			goto out;
		}
		memcpy(&zip_ctx->config, config, sizeof(struct wd_ctx_config));
	}
	return 0;
out:
	for (j = 0; j < i; j++) {
		h_qp = (handle_t)wd_ctx_get_priv(config->ctxs[j].ctx);
		hisi_qm_free_qp(h_qp);
	}
	return ret;
}

static void hisi_zip_exit(void *priv)
{
	struct hisi_zip_ctx *zip_ctx = (struct hisi_zip_ctx *)priv;
	struct wd_ctx_config *config = &zip_ctx->config;
	handle_t h_qp;
	int i;

	for (i = 0; i < config->ctx_num; i++) {
		h_qp = (handle_t)wd_ctx_get_priv(config->ctxs[i].ctx);
		hisi_qm_free_qp(h_qp);
	}
}


static int hisi_zip_comp_send(handle_t ctx, struct wd_comp_msg *msg)
{
	struct hisi_zip_sqe sqe;
	handle_t h_qp = (handle_t)wd_ctx_get_priv(ctx);
	__u8 flush_type;
	__u16 count = 0;
	void *ctx_buf;
	__u8 stream_pos;
	__u8 state;
	int ret;

	memset(&sqe, 0, sizeof(struct hisi_zip_sqe));
	switch (msg->alg_type) {
	case WD_ZLIB:
		sqe.dw9 = HW_ZLIB;
		break;
	case WD_GZIP:
		sqe.dw9 = HW_GZIP;
		break;
	default:
		return -WD_EINVAL;
	}

	sqe.source_addr_l = lower_32_bits((__u64)msg->src);
	sqe.source_addr_h = upper_32_bits((__u64)msg->src);
	sqe.dest_addr_l = lower_32_bits((__u64)msg->dst);
	sqe.dest_addr_h = upper_32_bits((__u64)msg->dst);
	if (msg->ctx_buf) {
		ctx_buf = msg->ctx_buf + 64;  /* reserve 64 BYTE for ctx_dwx*/
		sqe.stream_ctx_addr_l = lower_32_bits((__u64)ctx_buf);
		sqe.stream_ctx_addr_h = upper_32_bits((__u64)ctx_buf);
	} else {
		sqe.stream_ctx_addr_l = 0;
		sqe.stream_ctx_addr_h = 0;
	}

	state = (msg->stream_mode == WD_COMP_STATEFUL) ? HZ_STATEFUL :
		HZ_STATELESS;
	stream_pos = (msg->stream_pos == WD_COMP_STREAM_NEW) ? HZ_STREAM_NEW :
		    HZ_STREAM_OLD;
	flush_type = (msg->flush_type == WD_FINISH) ? HZ_FINISH :
		     HZ_SYNC_FLUSH;

	sqe.dw7 |= ((stream_pos << STREAM_POS_SHIFT) |
		    (state << STREAM_MODE_SHIFT) |
		    (flush_type)) << STREAM_FLUSH_SHIFT;
	sqe.input_data_length = msg->in_size;
	if (msg->avail_out > MIN_AVAILOUT_SIZE)
		sqe.dest_avail_out = msg->avail_out;
	else
		sqe.dest_avail_out = MIN_AVAILOUT_SIZE;
	if (msg->ctx_buf) {
		/* ctx_dwx uses 4 BYTES */
		sqe.ctx_dw0 = *(__u32 *)msg->ctx_buf;
		sqe.ctx_dw1 = *(__u32 *)(msg->ctx_buf + 4);
		sqe.ctx_dw2 = *(__u32 *)(msg->ctx_buf + 8);
	}
	sqe.isize = msg->isize;
	sqe.checksum = msg->checksum;
	sqe.tag = msg->tag;

	ret = hisi_qm_send(h_qp, &sqe, 1, &count);
	if (ret < 0) {
		WD_ERR("hisi_qm_send is err(%d)!\n", ret);
		return ret;
	}

	return ret;
}

static int hisi_zip_comp_recv(handle_t ctx, struct wd_comp_msg *recv_msg)
{
	struct hisi_zip_sqe sqe;
	int ret;
	__u16 count = 0;
	handle_t h_qp = (handle_t)wd_ctx_get_priv(ctx);

	ret = hisi_qm_recv(h_qp, &sqe, 1, &count);
	if (ret < 0) {
		if (ret != -EAGAIN)
			WD_ERR("hisi_qm_recv is err(%d)!\n", ret);
		return ret;
	}

	__u16 ctx_st = sqe.ctx_dw0 & HZ_CTX_ST_MASK;
	__u32 status = sqe.dw3 & HZ_STATUS_MASK;
	__u32 type = sqe.dw9 & HZ_REQ_TYPE_MASK;

	if (status != 0 && status != HZ_NEGACOMPRESS &&
	    status != HZ_CRC_ERR && status != HZ_DECOMP_END) {
		WD_ERR("bad status(ctx_st=0x%x, s=0x%x, t=%u)\n",
		       ctx_st, status, type);
		recv_msg->status = WD_IN_EPARA;
	} else {
		if (!sqe.consumed || !sqe.produced)
			return -EAGAIN;
		recv_msg->status = 0;
	}
	recv_msg->in_cons = sqe.consumed;
	recv_msg->produced = sqe.produced;
	recv_msg->avail_out = sqe.dest_avail_out;
	if (sqe.stream_ctx_addr_l && sqe.stream_ctx_addr_h) {
		/*
		 * In ASYNC mode, recv_msg->ctx_buf is NULL.
		 * recv_msg->ctx_buf is only valid in SYNC mode.
		 */
		*(int *)recv_msg->ctx_buf = sqe.ctx_dw0;
		*(int *)(recv_msg->ctx_buf + 4) = sqe.ctx_dw1;
		*(int *)(recv_msg->ctx_buf + 8) = sqe.ctx_dw2;
	}
	recv_msg->isize = sqe.isize;
	recv_msg->checksum = sqe.checksum;
	recv_msg->tag = sqe.tag;

	return 0;

}

static struct wd_comp_driver hisi_zip = {
	.drv_name		= "hisi_zip",
	.alg_name		= "zlib\ngzip",
	.drv_ctx_size		= sizeof(struct hisi_zip_ctx),
	.init			= hisi_zip_init,
	.exit			= hisi_zip_exit,
	.comp_send		= hisi_zip_comp_send,
	.comp_recv		= hisi_zip_comp_recv,
};

WD_COMP_SET_DRIVER(hisi_zip);
