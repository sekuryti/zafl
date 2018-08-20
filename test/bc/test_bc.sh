export AFL_TIMEOUT=60
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 

session=/tmp/tmp.bc.$$

cleanup()
{
	rm -fr /tmp/gzip.tmp* gzip*.zafl peasoup_exec*.gzip* zafl_in zafl_out $session
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

mkdir $session
pushd $session

# build ZAFL version of bc executable
$PSZ `which bc` bc.stars.zafl -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zipr:--stars 
if [ $? -eq 0 ]; then
	log_success "build bc.stars.zafl"
else
	log_error "build bc.stars.zafl"
fi

# build ZAFL version of readline shared library
readline=$( ldd `which bc` | grep libreadline | cut -d'>' -f2 | cut -d'(' -f1 )
readline_basename=$( basename $readline )
readline_realpath=$( realpath $readline )
echo "basename: $readline_basename  realpath: $readline_realpath"
$PSZ $readline_realpath $readline_basename -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zipr:--stars 
if [ $? -eq 0 ]; then
	log_success "build zafl version of $readline_basename at $readline_realpath"
else
	log_error "build zafl version of $readline_basename at $readline_realpath"
fi

ls -lt

fuzz_with_zafl $(realpath ./bc.stars.zafl)

popd
