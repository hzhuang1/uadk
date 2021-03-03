AM_CFLAGS=-Wall -O0 -fno-strict-aliasing -I../include -I../ -I lib -pthread

bin_PROGRAMS=test_hisi_zip test_hisi_zlib wd_zip_test

test_hisi_zip_SOURCES=test_hisi_zip.c
if WD_STATIC_DRV
test_hisi_zip_LDADD=../../.libs/libwd.la
else
test_hisi_zip_LDADD= -L../../.libs -lwd
endif	# WD_STATIC_DRV

test_hisi_zlib_SOURCES=test_hisi_zlib.c zip_alg.c
if WD_STATIC_DRV
test_hisi_zlib_LDADD=../../.libs/libwd.la
else
test_hisi_zlib_LDADD= -L../../.libs -lwd
endif	# WD_STATIC_DRV

wd_zip_test_SOURCES=wd_zip_test.c zip_alg.c
if WD_STATIC_DRV
wd_zip_test_LDADD=../../.libs/libwd.la -lz
else
wd_zip_test_LDADD= -L../../.libs -lwd -lz
endif	# WD_STATIC_DRV

SUBDIRS=. test_mm bmm_test

if WITH_OPENSSL_DIR
SUBDIRS+=hisi_hpre_test
endif
