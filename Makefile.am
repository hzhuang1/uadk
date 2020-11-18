ACLOCAL_AMFLAGS = -I m4 -I./include
AUTOMAKE_OPTIONS = foreign subdir-objects
AM_CFLAGS=-Wall -fno-strict-aliasing -I./include

if WITH_LOG_FILE
AM_CFLAGS+=-DWITH_LOG_FILE=\"$(with_log_file)\"
endif

if WITH_UADK_V1

SUBDIRS= v1

#V1=v1

#lib_LTLIBRARIES=libwd.la
#libwd_la_SOURCES=$(V1)/wd.c $(V1)/wd_adapter.c $(V1)/wd.h $(V1)/wd_adapter.h \
#	$(V1)/smm.c $(V1)/smm.h \
#	$(V1)/bmm.c $(V1)/bmm.h \
#	$(V1)/wd_sched.c $(V1)/wd_sched.h \
#	$(V1)/wd_rsa.c $(V1)/wd_rsa.h \
#	$(V1)/wd_dh.c $(V1)/wd_dh.h \
#	$(V1)/wd_comp.c $(V1)/wd_comp.h \
#	$(V1)/wd_cipher.c $(V1)/wd_cipher.h \
#	$(V1)/wd_digest.c $(V1)/wd_digest.h \
#	$(V1)/wd_util.c $(V1)/wd_util.h \
#	$(V1)/wd_bmm.c $(V1)/wd_bmm.h \
#	$(V1)/wd_ec.c $(V1)/wd_ec.h $(V1)/wd_ec_util.h \
#	$(V1)/drv/dummy_drv.c $(V1)/drv/dummy_drv.h \
#	$(V1)/drv/hisi_qm_udrv.c $(V1)/drv/hisi_qm_udrv.h \
#	$(V1)/drv/hisi_zip_udrv.c $(V1)/drv/hisi_zip_udrv.h \
#	$(V1)/drv/hisi_hpre_udrv.c $(V1)/drv/hisi_hpre_udrv.h \
#	$(V1)/drv/hisi_rde_udrv.c $(V1)/drv/hisi_rde_udrv.h \
#	$(V1)/drv/hisi_sec_udrv.c $(V1)/drv/hisi_sec_udrv.h

else
if WD_STATIC_DRV
AM_CFLAGS+=-DWD_STATIC_DRV
endif

include_HEADERS = include/wd.h include/wd_cipher.h include/wd_comp.h \
		  include/wd_dh.h include/wd_digest.h include/wd_rsa.h \
		  include/uacce.h include/wd_alg_common.h

lib_LTLIBRARIES=libwd.la libwd_comp.la libwd_crypto.la libhisi_zip.la \
		libhisi_hpre.la libhisi_sec.la

libwd_la_SOURCES=wd.c wd.h

libwd_comp_la_SOURCES=wd_comp.c wd_comp.h wd_comp_drv.h wd_util.c wd_util.h
libwd_comp_la_LIBADD= -lwd -ldl

libhisi_zip_la_SOURCES=drv/hisi_comp.c hisi_comp.h drv/hisi_qm_udrv.c \
		hisi_qm_udrv.h wd_comp_drv.h
libhisi_zip_la_LIBADD= -ldl

libwd_crypto_la_SOURCES=wd_cipher.c wd_cipher.h wd_cipher_drv.h \
			wd_aead.c wd_aead.h wd_aead_drv.h \
			wd_rsa.c wd_rsa.h wd_rsa_drv.h \
			wd_dh.c wd_dh.h wd_dh_drv.h \
			wd_digest.c wd_digest.h wd_digest_drv.h \
			wd_util.c wd_util.h
libwd_crypto_la_LIBADD= -lwd -ldl -lnuma

libhisi_sec_la_SOURCES=drv/hisi_sec.c drv/hisi_qm_udrv.c \
		hisi_qm_udrv.h wd_cipher_drv.h wd_aead_drv.h
libhisi_sec_la_LIBADD= -lwd -lwd_crypto

libhisi_hpre_la_SOURCES=drv/hisi_hpre.c drv/hisi_qm_udrv.c \
		hisi_qm_udrv.h wd_hpre_drv.h
libhisi_hpre_la_LIBADD= -lwd -lwd_crypto
SUBDIRS=. test

endif
