#!/bin/bash -x 

TMP_FILE_1=/tmp/gzip.tmp.$$
TMP_FILE_2=/tmp/gzip.tmp.$$

cleanup()
{
	rm -fr /tmp/gzip.tmp* gzip*.zafl peasoup_exec*.gzip*
}

log_error()
{
	echo "FAIL: $1"
	cleanup
	exit 1
}

log_message()
{
	echo "$1"
}

log_success()
{
	echo "PASS: $1"
}

setup()
{
	echo "hello" > $TMP_FILE_1
	echo "hello" > $TMP_FILE_2
}

build_zafl()
{
	gzip_zafl=$1
	shift
	$PSZ `which gzip` $gzip_zafl -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true $*
	if [ ! $? -eq 0 ]; then
		log_error "$gzip_zafl: unable to generate Zafl version"	
	else
		log_message "$gzip_zafl: built successfully"
	fi
}

test_zafl()
{
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/ $1 $TMP_FILE_1
	if [ ! $? -eq 0 ]; then
		log_error "$1: unable to gzip file using zafl"
	fi

	gunzip  ${TMP_FILE_1}.gz
	diff $TMP_FILE_1 $TMP_FILE_2
	if [ $? -eq 0 ]; then
		log_success "$1: after unzipping, we get the same file back. yeah!"
	else
		log_error "$1: after unzipping, we get a diferent file"
	fi
}

pushd /tmp

setup
build_zafl gzip.nostars.zafl
test_zafl ./gzip.nostars.zafl
cleanup

setup
build_zafl gzip.zafl
test_zafl ./gzip.zafl
cleanup

log_success "all tests passed: zafl instrumentation operational on gzip"

