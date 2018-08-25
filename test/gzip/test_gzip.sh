AFL_TIMEOUT=30
session=/tmp/tmp.gzip.$$
TMP_FILE_1="${session}/gzip.tmp.$$"
TMP_FILE_2="${session}/gzip.tmp.$$"

mkdir -p $session

cleanup()
{
	rm -fr /tmp/gzip.tmp* gzip*.zafl peasoup_exec*.gzip* zafl_in zafl_out ${session}
}

log_error()
{
	echo "TEST FAIL: $1"
	exit 1
}

log_message()
{
	echo "TEST  MSG: $1"
}

log_success()
{
	echo "TEST PASS: $1"
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
		log_error "$gzip_zafl: unable to generate zafl version"	
	else
		log_message "$gzip_zafl: built successfully"
	fi
}

test_zafl()
{
	gzip_zafl=$( realpath $1 )
	shift

	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/ $gzip_zafl $* $TMP_FILE_1
	if [ ! $? -eq 0 ]; then
		log_error "$gzip_zafl $*: unable to gzip file using zafl version"
	fi

	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/ $gzip_zafl -d ${TMP_FILE_1}.gz
	diff $TMP_FILE_1 $TMP_FILE_2
	if [ $? -eq 0 ]; then
		log_success "$gzip_zafl $*: after zipping and unzipping, we get the same file back. yeah!"
	else
		log_error "$gzip_zafl $*: after zipping and unzipping, we get a diferent file"
	fi
}

fuzz_with_zafl()
{
	gzip_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/ timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $gzip_zafl -f
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$gzip_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$gzip_zafl: ran zafl binary: $execs_per_sec"
	else
		log_error "$gzip_zafl: unable to run with afl"
	fi

}

pushd ${session}
setup


# test setting of entry point via address
ep=$( objdump -Mintel -d /bin/gzip | grep text | grep -v -i disassembly | cut -d' ' -f1 | sed 's/^00000000//g' )
build_zafl gzip.stars.entrypoint.${ep}.zafl -o zafl:--stars -o "zafl:--entrypoint=$ep"
test_zafl ./gzip.stars.entrypoint.${ep}.zafl --fast

# test setting of entry point via function name
build_zafl gzip.entrypoint.zafl -o "zafl:--entrypoint=main"
test_zafl ./gzip.entrypoint.zafl --best

# test non-STARS version
build_zafl gzip.nostars.zafl
test_zafl ./gzip.nostars.zafl
test_zafl ./gzip.nostars.zafl --fast
test_zafl ./gzip.nostars.zafl --best

# test STARS version
build_zafl gzip.stars.zafl -o zafl:--stars
test_zafl ./gzip.stars.zafl
test_zafl ./gzip.stars.zafl --fast
test_zafl ./gzip.stars.zafl --best

# test STARS version on AFL
log_message "Fuzz for $AFL_TIMEOUT seconds"
fuzz_with_zafl ./gzip.stars.zafl

log_success "all tests passed: zafl instrumentation operational on gzip"

cleanup
popd
