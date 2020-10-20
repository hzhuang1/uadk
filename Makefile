CC		:= gcc
CFLAGS		:= -Werror
INCLUDES	:= -I./include -I. -Wall
LD		:= ld

RM		:= rm -f
LN		:= ln -sf
INSTALL		:= install
MAJOR		:= 0
MINOR		:= 0
REVISION	:= 0
VERSION		:= $(MAJOR).$(MINOR).$(REVISION)

SOURCE_DIR	:= . drv test test/hisi_hpre_test test/hisi_sec_test	\
		   test/hisi_zip_test
LIB_DIR		:= /usr/local/lib
APP_DIR		:= /usr/local/bin
TARGET_APP	:= test_hisi_sec test_hisi_hpre zip_sva_perf
TARGET_DYNLIB	:= libwd.so.$(VERSION) libwd_crypto.so.$(VERSION)	\
		   libwd_comp.so.$(VERSION) libhisi_sec.so.$(VERSION)	\
		   libhisi_hpre.so.$(VERSION) libhisi_zip.so.$(VERSION)
DYNLIB_MAJOR	:= libwd.so.$(MAJOR) libwd_crypto.so.$(MAJOR)		\
		   libwd_comp.so.$(MAJOR) libhisi_sec.so.$(MAJOR)	\
		   libhisi_hpre.so.$(MAJOR) libhisi_zip.so.$(MAJOR)
DYNLIB_SHORT	:= libwd.so libwd_crypto.so libwd_comp.so		\
		   libhisi_sec.so libhisi_hpre.so libhisi_zip.so

all: $(TARGET_DYNLIB) $(TARGET_APP)
libwd.so.$(VERSION): wd.o
	$(CC) -shared -fPIC -o $@ $?
	$(LN) libwd.so.$(VERSION) libwd.so

libwd_crypto.so.$(VERSION): wd_aead.o wd_cipher.o wd_digest.o wd_util.o	\
			    wd_rsa.o wd_dh.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -lnuma -ldl
	$(LN) libwd_crypto.so.$(VERSION) libwd_crypto.so

libwd_comp.so.$(VERSION): wd_comp.o wd_util.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -ldl
	$(LN) libwd_comp.so.$(VERSION) libwd_comp.so

libhisi_sec.so.$(VERSION): drv/hisi_sec.o drv/hisi_qm_udrv.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -lwd_crypto
	$(LN) libhisi_sec.so.$(VERSION) libhisi_sec.so

libhisi_hpre.so.$(VERSION): drv/hisi_hpre.o drv/hisi_qm_udrv.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd -lwd_crypto
	$(LN) libhisi_hpre.so.$(VERSION) libhisi_hpre.so

libhisi_zip.so.$(VERSION): drv/hisi_comp.o drv/hisi_qm_udrv.o
	$(CC) -shared -fPIC -o $@ $? -L. -lwd
	$(LN) libhisi_zip.so.$(VERSION) libhisi_zip.so

test/hisi_sec_test/test_hisi_sec.o: test/hisi_sec_test/test_hisi_sec.c
	$(CC) $(INCLUDES) -c $< -o $@ -L. -lwd_crypto

test_hisi_sec: test/hisi_sec_test/test_hisi_sec.o test/sched_sample.o
	$(CC) -Wl,-rpath=/usr/local/lib -o $@ $? -L. -lwd -lwd_crypto -lpthread

test/hisi_hpre_test/test_hisi_hpre.o: test/hisi_hpre_test/test_hisi_hpre.c
	$(CC) $(INCLUDES) -c $< -o $@ -L. -lwd_crypto

test_hisi_hpre: test/hisi_hpre_test/test_hisi_hpre.o test/sched_sample.o
	$(CC) -Wl,-rpath=/usr/local/lib -o $@ $? -L. -lwd -lwd_crypto -lpthread

test/hisi_zip_test/test_sva_perf.o: test/hisi_zip_test/test_sva_perf.c
	$(CC) $(INCLUDES) -DUSE_ZLIB -c $< -o $@ -L. -lwd -lwd_comp

test/hisi_zip_test/test_lib.o: test/hisi_zip_test/test_lib.c
	$(CC) $(INCLUDES) -DUSE_ZLIB -c $< -o $@ -L. -lwd -lwd_comp

test/hisi_zip_test/sva_file_test.o: test/hisi_zip_test/sva_file_test.c
	$(CC) $(INCLUDES) -c $< -o $@ -L. -lwd -lwd_comp

zip_sva_perf: test/hisi_zip_test/test_sva_perf.o test/sched_sample.o	\
	       test/hisi_zip_test/test_lib.o test/hisi_zip_test/sva_file_test.o
	$(CC) -Wl,-rpath=/usr/local/lib -o $@ $? -L. -lwd -lwd_comp	\
		-lpthread -lm -lz

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
		$(RM) $$d/*.so.*;		\
	done


#############################################################################
# install
install:
	$(INSTALL) -m 755 -t $(LIB_DIR) $(TARGET_DYNLIB)
	$(INSTALL) -m 755 -t $(APP_DIR) $(TARGET_APP)
	#PATH="$(PATH):/sbin" ldconfig -v -n $(LIB_DIR)
	# Fail to use ldconfig. Use ln instead.
	export PATH="$(PATH):/sbin"
	$(LN) $(LIB_DIR)/libwd.so.$(VERSION) $(LIB_DIR)/libwd.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libwd.so.$(VERSION) $(LIB_DIR)/libwd.so
	$(LN) $(LIB_DIR)/libwd_comp.so.$(VERSION) $(LIB_DIR)/libwd_comp.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libwd_comp.so.$(VERSION) $(LIB_DIR)/libwd_comp.so
	$(LN) $(LIB_DIR)/libwd_crypto.so.$(VERSION) $(LIB_DIR)/libwd_crypto.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libwd_crypto.so.$(VERSION) $(LIB_DIR)/libwd_crypto.so
	$(LN) $(LIB_DIR)/libhisi_hpre.so.$(VERSION) $(LIB_DIR)/libhisi_hpre.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libhisi_hpre.so.$(VERSION) $(LIB_DIR)/libhisi_hpre.so
	$(LN) $(LIB_DIR)/libhisi_sec.so.$(VERSION) $(LIB_DIR)/libhisi_sec.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libhisi_sec.so.$(VERSION) $(LIB_DIR)/libhisi_sec.so
	$(LN) $(LIB_DIR)/libhisi_zip.so.$(VERSION) $(LIB_DIR)/libhisi_zip.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libhisi_zip.so.$(VERSION) $(LIB_DIR)/libhisi_zip.so
