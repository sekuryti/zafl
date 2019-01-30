export AFL_TIMEOUT=15
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 

user=$(whoami)
session=/tmp/tmp.${user}.zafl.bc.fixed_map.$$

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

# Fix map at 0x12000
trace_map_address="0x12000"
export ZAFL_TRACE_MAP_FIXED_ADDRESS="$trace_map_address"

# build ZAFL version of bc executable with fixed map

zafl.sh `which bc` bc.fixed.zafl -m $trace_map_address --tempdir analysis.bc.fixed.zafl
if [ $? -eq 0 ]; then
	log_success "build bc.fixed.zafl"
else
	log_error "build bc.fixed.zafl"
fi
grep ATTR analysis.bc.fixed.zafl/logs/zafl.log
log_message "Fuzz for $AFL_TIMEOUT secs"
fuzz_with_zafl $(realpath ./bc.fixed.zafl)

# build ZAFL version of readline shared library
readline=$( ldd `which bc` | grep libreadline | cut -d'>' -f2 | cut -d'(' -f1 )
readline_basename=$( basename $readline )
readline_realpath=$( realpath $readline )
echo "basename: $readline_basename  realpath: $readline_realpath"
zafl.sh $readline_realpath $readline_basename -m $trace_map_address
if [ $? -eq 0 ]; then
	log_success "build zafl version of $readline_basename at $readline_realpath"
else
	log_error "build zafl version of $readline_basename at $readline_realpath"
fi

ldd bc.fixed.zafl

log_message "Fuzz for $AFL_TIMEOUT secs (with readline library zafl'ed)"
fuzz_with_zafl $(realpath ./bc.fixed.zafl)

# test functionality
echo "2+3" | `which bc` > out.bc.orig
echo "2+3" | ./bc.fixed.zafl > out.bc.fixed.zafl
diff out.bc.orig out.bc.fixed.zafl >/dev/null 2>&1
if [ $? -eq 0 ]; then
	log_success "bc.fixed.zafl basic functionality"
else
	log_error "bc.fixed.zafl basic functionality"
fi

popd

cleanup
