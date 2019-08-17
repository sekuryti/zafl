AFL_TIMEOUT=15
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
session=/tmp/tmp.ls.$$
TMP_FILE_1="${session}/ls.tmp.$$"

mkdir -p $session

cleanup()
{
	rm -fr /tmp/ls.tmp* ls*.zafl peasoup_exec*.ls* zafl_in zafl_out ${session}
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
}

build_zuntracer()
{
	ls_zafl=$1
	shift
	zafl.sh `which ls` $ls_zafl --untracer --tempdir analysis.${ls_zafl} $*
	if [ ! $? -eq 0 ]; then
		log_error "$ls_zafl: unable to generate zafl version"	
	else
		log_message "$ls_zafl: built successfully"
	fi

	grep ATTR analysis.${ls_zafl}/logs/zax.log
	if [ ! $? -eq 0 ]; then
		log_error "$ls_zafl: no attributes or zax.log file produced"
	fi

	if [ ! -f analysis.${ls_zafl}/zax.map ]; then
		log_error "$ls_zafl: zax.map file not found"
	fi
}

test_zuntracer()
{
	ls_zafl=$( realpath $1 )
	shift
	$ls_zafl $* $TMP_FILE_1
	if [ ! $? -eq 0 ]; then
		log_error "$ls_zafl $*: unable to ls file using zafl version"
	fi
}

fuzz_with_zafl()
{
	ls_zafl=$1
	shift

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/ timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $ls_zafl $*
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$ls_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$ls_zafl $*: ran zafl binary: $execs_per_sec"
	else
		log_error "$ls_zafl $*: unable to run with afl"
	fi

}

pushd ${session}
setup

# zuntracer, don't break critical edges
build_zuntracer ls.untracer.no_critical_edge -C
test_zuntracer ./ls.untracer.no_critical_edge -lt

# zuntracer, do break all critical edges
build_zuntracer ls.untracer.critical_edge -c all
test_zuntracer ./ls.untracer.critical_edge -lt

# zuntracer, do break target critical edges
build_zuntracer ls.untracer.critical_edge -c targets
test_zuntracer ./ls.untracer.critical_edge -lt

# zuntracer, do break fallthrough critical edges
build_zuntracer ls.untracer.critical_edge -c fallthroughs
test_zuntracer ./ls.untracer.critical_edge -lt

# zuntracer, do break critical edges, optimize graph
build_zuntracer ls.untracer.critical_edge.graph -c all -g -M
test_zuntracer ./ls.untracer.critical_edge.graph -lt

# zuntracer, do break critical edges, optimize graph
build_zuntracer ls.untracer.critical_edge.graph -c target -g -M
test_zuntracer ./ls.untracer.critical_edge.graph -lt

# zuntracer, do break critical edges, optimize graph
build_zuntracer ls.untracer.critical_edge.graph -c fallthrough -g -M
test_zuntracer ./ls.untracer.critical_edge.graph -lt

log_message "Fuzz zuntracer (basic block coverage) for $AFL_TIMEOUT seconds"
fuzz_with_zafl ./ls.untracer.no_critical_edge -lt

log_message "Fuzz zuntracer (break critical edges) for $AFL_TIMEOUT seconds"
fuzz_with_zafl ./ls.untracer.critical_edge -lt 

log_message "Fuzz zuntracer (break critical edges + graph optimization) for $AFL_TIMEOUT seconds"
fuzz_with_zafl ./ls.untracer.critical_edge.graph -lt

build_zuntracer ls.untracer.fixed.0x10000 -m 0x10000
test_zuntracer ./ls.untracer.fixed.0x10000 -lt

fuzz_with_zafl ./ls.untracer.fixed.0x10000 -lt

log_success "all tests passed: zafl/zuntracer instrumentation operational on ls"

cleanup
popd
