CC		:= gcc
CFLAGS		:= -Werror
INCLUDES	:= -I./include -I. -Wall

SOURCE_DIR	:= . drv test test/hisi_hpre_test test/hisi_sec_test	\
		   test/hisi_zip_test
TARGET_APP	:= test_hisi_sec
TARGET_DYNLIB	:= libwd.so libwd_crypto.so libhisi_sec.so

RM		:= rm -f

all: $(TARGET_APP) $(TARGET_DYNLIB)
libwd.so: wd.o
	$(CC) -shared -fPIC -o $@ $?

libwd_crypto.so: wd_aead.o wd_cipher.o wd_digest.o wd_util.o
	$(CC) -shared -fPIC -o $@ $? -ldl -L. -lwd -lnuma

libhisi_sec.so: drv/hisi_sec.o drv/hisi_qm_udrv.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -lwd_crypto

test/hisi_sec_test/test_hisi_sec.o: test/hisi_sec_test/test_hisi_sec.c
	$(CC) $(INCLUDES) -c $< -o $@ -L. -lwd_crypto

test_hisi_sec: test/hisi_sec_test/test_hisi_sec.o test/sched_sample.o
	$(CC) -o $@ $? -L. -lwd -lwd_crypto -lpthread

%.o: %.c
	$(CC) $(INCLUDES) -fPIC -c $< -o $@

#############################################################################
# clean
clean:
	for d in $(SOURCE_DIR);			\
	do					\
		$(RM) $$d/*.a;			\
		$(RM) $$d/*.o;			\
		$(RM) $$d/*.so;			\
	done
