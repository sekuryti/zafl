export AFL_TIMEOUT=15
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 

session=/tmp/tmp.od.$$

cleanup()
{
	rm -fr /tmp/od.tmp* od*.zafl peasoup_exec*.od* zafl_in zafl_out $session
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

fuzz_with_zafl()
{
	od_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $od_zafl @@
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$od_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$od_zafl: $execs_per_sec"
	else
		log_error "$od_zafl: unable to run with afl"
	fi

}

mkdir $session
pushd $session

# build ZAFL (with graph optimizations) version of od executable
zafl.sh `which od` od.zafl.d.g -d -g --tempdir analysis.od.zafl.d.g
if [ $? -eq 0 ]; then
	log_success "build od.zafl.d.g"
else
	log_error "build od.zafl.d.g"
fi
grep ATTR analysis.od.zafl.d.g/logs/zax.log

log_message "Fuzz rida.zafl for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./od.zafl.d.g)

cleanup
popd
