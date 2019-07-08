export AFL_TIMEOUT=15
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 

session=/tmp/tmp.sha256sum.$$

cleanup()
{
	rm -fr /tmp/sha256sum.tmp* sha256sum*.zafl peasoup_exec*.sha256sum* zafl_in zafl_out $session
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
	sha256sum_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $sha256sum_zafl 
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$sha256sum_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$sha256sum_zafl: $execs_per_sec"
	else
		log_error "$sha256sum_zafl: unable to run with afl"
	fi

}

mkdir $session
pushd $session

# build ZAFL (no Ida) version of sha256sum executable
zafl.sh `which sha256sum` sha256sum.rida.zafl --tempdir analysis.sha256sum
if [ $? -eq 0 ]; then
	log_success "build sha256sum.rida.zafl"
else
	log_error "build sha256sum.rida.zafl"
fi
grep ATTR analysis.sha256sum/logs/zax.log
if [ ! $? -eq 0 ]; then
	log_error "zax transform did not run"
fi

log_message "Fuzz rida.zafl for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./sha256sum.rida.zafl)

# build ZAFL (with Ida) version of sha256sum executable
zafl.sh `which sha256sum` sha256sum.ida.zafl --tempdir analysis.sha256sum.ida
if [ $? -eq 0 ]; then
	log_success "build sha256sum.ida.zafl"
else
	log_error "build sha256sum.ida.zafl"
fi
grep ATTR analysis.sha256sum.ida/logs/zax.log
if [ ! $? -eq 0 ]; then
	log_error "zax transform did not run"
fi

log_message "Fuzz ida.zafl for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./sha256sum.ida.zafl)

cleanup
popd
