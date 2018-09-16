export AFL_TIMEOUT=15
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 

session=/tmp/tmp.strings.$$

cleanup()
{
	rm -fr /tmp/strings.tmp* strings*.zafl peasoup_exec*.strings* zafl_in zafl_out $session
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
	strings_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $strings_zafl 
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$strings_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$strings_zafl: $execs_per_sec"
	else
		log_error "$strings_zafl: unable to run with afl"
	fi

}

mkdir $session
pushd $session

# build ZAFL version of strings executable
zafl.sh `which strings` strings.zafl --tempdir analysis.strings.zafl
if [ $? -eq 0 ]; then
	log_success "build strings.zafl"
else
	log_error "build strings.zafl"
fi
grep ATTR analysis.strings.zafl/logs/zafl.log

log_message "Fuzz for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./strings.zafl)

# build ZAFL (no Ida) version of strings executable
zafl.sh `which strings` strings.rida.zafl --tempdir analysis.strings.rida.zafl
if [ $? -eq 0 ]; then
	log_success "build strings.rida.zafl"
else
	log_error "build strings.rida.zafl"
fi
grep ATTR analysis.strings.rida.zafl/logs/zafl.log

log_message "Fuzz rida.zafl for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./strings.rida.zafl)

cleanup
popd
