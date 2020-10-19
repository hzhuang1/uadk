CC		:= gcc
CFLAGS		:= -Werror
INCLUDES	:= -I./include -I. -Wall

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
TARGET_APP	:= test_hisi_sec
TARGET_DYNLIB	:= libwd.so.$(VERSION) libwd_crypto.so.$(VERSION)	\
		   libhisi_sec.so.$(VERSION)
DYNLIB_MAJOR	:= libwd.so.$(MAJOR) libwd_crypto.so.$(MAJOR)		\
		   libhisi_sec.so.$(MAJOR)
DYNLIB_SHORT	:= libwd.so libwd_crypto.so libhisi_sec.so

all: $(TARGET_APP) $(TARGET_DYNLIB)
libwd.so.$(VERSION): wd.o
	$(CC) -shared -fPIC -o $@ $?

libwd_crypto.so.$(VERSION): wd_aead.o wd_cipher.o wd_digest.o wd_util.o
	$(CC) -shared -fPIC -o $@ $? -ldl -L. -lwd -lnuma

libhisi_sec.so.$(VERSION): drv/hisi_sec.o drv/hisi_qm_udrv.o
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
	$(LN) $(LIB_DIR)/libwd_crypto.so.$(VERSION) $(LIB_DIR)/libwd_crypto.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libwd_crypto.so.$(VERSION) $(LIB_DIR)/libwd_crypto.so
	$(LN) $(LIB_DIR)/libhisi_sec.so.$(VERSION) $(LIB_DIR)/libhisi_sec.so.$(MAJOR)
	$(LN) $(LIB_DIR)/libhisi_sec.so.$(VERSION) $(LIB_DIR)/libhisi_sec.so
