#!/bin/bash -e

#VALGRIND="valgrind --tool=memcheck --leak-check=yes"
RM="sudo rm"
CP="sudo cp"
CHMOD="sudo chmod"

WORKSPACE=/home/hzhuang1
LIB_ROOT=${WORKSPACE}/uadk-dynamic-v2
LIB_DIR=usr/local/lib
BIN_DIR=usr/local/bin
INC_DIR=usr/local/include

HWZIP="sudo LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
	  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
	  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
	  ${VALGRIND} ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf"

#sudo rm origin ori.md5 /tmp/ori.gz

# arg1: source file, arg2: destination file, arg3: algorithm type
hw_blk_deflate()
{
	case $3 in
	"gzip")
		${RM} -f /tmp/gzip_list.bin
		${HWZIP} --in $1 --out $2 --olist /tmp/gzip_list.bin $@
		;;
	"zlib")
		${HWZIP} -z --in $1 --out $2 --olist /tmp/zlib_list.bin $@
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
hw_blk_inflate()
{
	case $3 in
	"gzip")
		${HWZIP} -d --in $1 --out $2 --ilist /tmp/gzip_list.bin $@
		;;
	"zlib")
		${HWZIP} -z -d --in $1 --out $2 --ilist /tmp/zlib_list.bin $@
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
hw_strm_deflate()
{
	case $3 in
	"gzip")
		${HWZIP} -S --in $1 --out $2 $@
		;;
	"zlib")
		${HWZIP} -z -S --in $1 --out $2 $@
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
hw_strm_inflate()
{
	case $3 in
	"gzip")
		${HWZIP} -S -d --in $1 --out $2 $@
		;;
	"zlib")
		${HWZIP} -z -S -d --in $1 --out $2 $@
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type,
# arg4: block size
sw_blk_deflate()
{
	case $3 in
	"gzip")
		${RM} -f /tmp/gzip_list.bin
		echo "python ./list_loader.py --in $1 --out $2 --olist /tmp/gzip_list.bin -b $4"
		python ./list_loader.py --in $1 --out $2 --olist /tmp/gzip_list.bin -b $4
		#gzip -c --fast < $1 > $2
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
sw_blk_inflate()
{
	case $3 in
	"gzip")
		echo "python ./list_loader.py --in $1 --out $2 --ilist /tmp/gzip_list.bin"
		python ./list_loader.py --in $1 --out $2 --ilist /tmp/gzip_list.bin
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
sw_strm_deflate()
{
	case $3 in
	"gzip")
		gzip -c --fast < $1 > $2 || exit_code=$?
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: source file, arg2: destination file, arg3: algorithm type
sw_strm_inflate()
{
	case $3 in
	"gzip")
		gunzip < $1 > $2 || exit_code=$?
		;;
	*)
		echo "Unsupported algorithm type: $3"
		return -1
		;;
	esac
}

# arg1: random, arg2: indicates X MB of random file
# arg1: existed file name
prepare_src_file()
{
	case $1 in
	"random")
		dd if=/dev/urandom of=origin bs=1M count=$2 &> /dev/null
		;;
	*)
		${CP} $1 origin
		${CHMOD} 777 origin
		;;
	esac
}

# arg1: existed text file
hw_dfl_sw_ifl()
{
	${RM} -f origin /tmp/ori.gz ori.md5
	echo "hardware compress with gzip format and software decompress:"
	echo "1MB random data"
	# Generate random data with 1MB size
	prepare_src_file random 1
	md5sum origin > ori.md5

	hw_blk_deflate origin /tmp/ori.gz gzip -b 8192
	sw_blk_inflate /tmp/ori.gz origin gzip
	md5sum -c ori.md5
	echo "verified block for RANDOM"

	${RM} -f /tmp/ori.gz
	hw_strm_deflate origin /tmp/ori.gz gzip -b 8192
	sw_strm_inflate /tmp/ori.gz origin gzip
	md5sum -c ori.md5
	echo "verified stream for RANDOM"

	# Use existed text file. It's not in alignment.
	${RM} -f origin /tmp/ori.gz ori.md5
	prepare_src_file $1
	md5sum origin > ori.md5

	hw_blk_deflate origin /tmp/ori.gz gzip -b 8192
	sw_blk_inflate /tmp/ori.gz origin gzip
	md5sum -c ori.md5
	echo "verified block for file"

	# This case fails.
	${RM} -f /tmp/ori.gz
	hw_strm_deflate origin /tmp/ori.gz gzip -b 8192
	sw_strm_inflate /tmp/ori.gz origin gzip
	md5sum -c ori.md5
	echo "verified stream for file"
}

# arg1: existed text file
sw_dfl_hw_ifl()
{
	${RM} -f origin /tmp/ori.gz ori.md5
	echo "gzip compress and hardware decompress:"
	# Generate random data with 1MB size
	prepare_src_file random 1
	md5sum origin > ori.md5

	# Only gzip compress and hardware decompress
	sw_blk_deflate origin /tmp/ori.gz gzip 8192
	hw_blk_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5
	echo "BLOCK inflate for RANDOM"

	sw_strm_deflate origin /tmp/ori.gz gzip 8192
	hw_strm_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5

	# Use existed text file. It's not in alignment.
	${RM} -f origin /tmp/ori.gz ori.md5
	prepare_src_file $1
	md5sum origin > ori.md5

	# Only gzip compress and hardware decompress
	sw_strm_deflate origin /tmp/ori.gz gzip 8192
	hw_strm_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5
}

# arg1: existed text file
hw_dfl_hw_ifl()
{
	${RM} -f origin /tmp/ori.gz ori.md5
	echo "hardware compress and hardware decompress:"
	# Generate random data with 1MB size
	prepare_src_file random 1
	md5sum origin > ori.md5

	hw_blk_deflate origin /tmp/ori.gz gzip -b 8192
	hw_blk_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5
	echo "Pass RANDOM data for hw compress and hw decompress"

	${RM} -f /tmp/ori.gz
	hw_strm_deflate origin /tmp/ori.gz gzip -b 8192
	hw_strm_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5

	# Use existed text file. It's not in alignment.
	${RM} -f origin /tmp/ori.gz ori.md5
	prepare_src_file $1
	md5sum origin > ori.md5

	hw_strm_deflate origin /tmp/ori.gz gzip -b 8192
	hw_strm_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5

	${RM} -f /tmp/ori.gz
	hw_blk_deflate origin /tmp/ori.gz gzip -b 8192
	hw_blk_inflate /tmp/ori.gz origin gzip -b 8192
	md5sum -c ori.md5
}

if [ ! -z $1 ]; then
	${HWZIP} --self
	exit
fi
hw_dfl_sw_ifl /var/log/syslog
sw_dfl_hw_ifl /var/log/syslog
#hw_dfl_hw_ifl /var/log/syslog
exit

#echo "run without parameter"
#sudo \
#  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
#  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
#  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
#  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf

echo "hardware compress gzip and software decompress"
dd if=/dev/urandom of=origin bs=1M count=1 &> /dev/null
md5sum origin
md5sum origin > ori.md5
${HWZIP} --in origin --out /tmp/ori.gz
gunzip < /tmp/ori.gz > origin
md5sum -c ori.md5
md5sum origin
exit

#echo "start test"
#sudo \
#  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
#  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
#  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
#  test/sanity_test.sh

echo "Start to compress Makefile with BLOCK"
sudo \
  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf --in Makefile --out /tmp/x.zip -b 8192 -m 0
sudo \
  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -d --in /tmp/x.zip --out /tmp/M -b 8192
exit
echo "Start to compress Makefile with STREAM"
sudo \
  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf --in Makefile --out /tmp/x.zip -l 1000 -b 8192 -S
sudo \
  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -d --in /tmp/x.zip --out /tmp/M -l 1000 -b 8192 -S -t 10 -m 1
echo "Start to compress RANDOM DATA with BLOCK"
for thread in 1 2 4 8 16
do
	sudo \
	  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
	  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
	  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
	  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -z -t ${thread} -b 8192
	sudo \
	  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
	  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
	  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
	  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -d -t ${thread} -b 8192
	sudo \
	  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
	  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
	  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
	  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -z -t ${thread} -b 8192 -m 0
	sudo \
	  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
	  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
	  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
	  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -d -t ${thread} -b 8192 -m 0
done
exit

sudo \
  LD_LIBRARY_PATH=${LIB_ROOT}/${LIB_DIR} \
  PATH=${LIB_ROOT}/${BIN_DIR}/:${PATH} \
  C_INCLUDE_PATH=${LIB_ROOT}/${INC_DIR} \
  ${LIB_ROOT}/${BIN_DIR}/zip_sva_perf --self


  #${LIB_ROOT}/${BIN_DIR}/zip_sva_perf -s 0x1000 -b 0x1000 -k t
