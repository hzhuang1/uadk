CC		:= gcc
CFLAGS		:= -Werror
INCLUDES	:= -I./include -I. -Wall
TARGETS		:= libwd.so libwd_crypto.so libhisi_sec.so test_hisi_sec

all: $(TARGETS)
libwd.so: wd.o
	$(CC) -shared -fPIC -o $@ $?

libwd_crypto.so: wd_aead.o wd_cipher.o wd_digest.o wd_util.o
	$(CC) -shared -fPIC -o $@ $? -ldl -L. -lwd -lnuma

libhisi_sec.so: drv/hisi_sec.o drv/hisi_qm_udrv.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -lwd_crypto

test/hisi_sec_test/test_hisi_sec.o: test/hisi_sec_test/test_hisi_sec.c
	$(CC) $(INCLUDES) -c $< -o $@ -L. -lwd_crypto

test_hisi_sec: test/hisi_sec_test/test_hisi_sec.o test/sched_sample.o libwd.so libwd_crypto.so libhisi_sec.so
	$(CC) -o $@ $? -L. -lwd -lwd_crypto -lhisi_sec -lpthread

%.o: %.c
	$(CC) $(INCLUDES) -fPIC -c $< -o $@
