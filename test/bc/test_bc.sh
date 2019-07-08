export AFL_TIMEOUT=15
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1

user=$(whoami)
session=/tmp/tmp.${user}.zafl.bc.$$

cleanup()
{
	rm -fr $session
}

log_error()
{
	echo "TEST FAIL: $1"
	cleanup
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
	bc_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $bc_zafl 
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$bc_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$bc_zafl: $execs_per_sec"
	else
		log_error "$bc_zafl: unable to run with afl"
	fi

}

mkdir -p $session
pushd $session

# build with graph optimization
zafl.sh `which bc` bc.stars.zafl.d.g.r.cs -d -g --tempdir analysis.bc.stars.zafl.d.g.r.cs -r 123 --enable-context-sensitivity function
if [ $? -eq 0 ]; then
	log_success "build bc.stars.zafl.d.g.r.cs"
else
	log_error "build bc.stars.zafl.d.g.r.cs"
fi
log_message "Fuzz for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./bc.stars.zafl.d.g.r.cs)

# build ZAFL version of readline shared library
readline=$( ldd `which bc` | grep libreadline | cut -d'>' -f2 | cut -d'(' -f1 )
readline_basename=$( basename $readline )
readline_realpath=$( realpath $readline )
echo "basename: $readline_basename  realpath: $readline_realpath"
#$PSZ $readline_realpath $readline_basename -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zafl:--stars 
zafl.sh $readline_realpath $readline_basename --random-seed 42 -d
if [ $? -eq 0 ]; then
	log_success "build zafl version of $readline_basename at $readline_realpath"
else
	log_error "build zafl version of $readline_basename at $readline_realpath"
fi

# test functionality
echo "2+3" | `which bc` > out.bc.orig
echo "2+3" | ./bc.stars.zafl.d.g.r.cs > out.bc.stars.zafl.d.g.r.cs
diff out.bc.orig out.bc.stars.zafl.d.g.r.cs >/dev/null 2>&1
if [ $? -eq 0 ]; then
	log_success "bc.stars.zafl.d.g.r.cs basic functionality"
else
	log_error "bc.stars.zafl.d.g.r.cs basic functionality"
fi

popd

cleanup
