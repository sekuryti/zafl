#! /bin/bash

export AFL_TIMEOUT=15
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECURITY_TRANSFORMS_HOME/lib/:. 
export AFL_SKIP_CPUFREQ=1
export AFL_SKIP_BIN_CHECK=1
export AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1

TEST_SRC_DIR=$ZAFL_HOME/test/eightqueens

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
	queens_zafl=$1

	# setup AFL directories
	mkdir zafl_in
	echo "1" > zafl_in/1

	if [ -d zafl_out ]; then
		rm -fr zafl_out
	fi

	# run for 30 seconds
	timeout $AFL_TIMEOUT afl-fuzz -i zafl_in -o zafl_out -- $queens_zafl 
	if [ $? -eq 124 ]; then
		if [ ! -e zafl_out/fuzzer_stats ]; then
			log_error "$queens_zafl: something went wrong with afl -- no fuzzer stats file"
		fi

		cat zafl_out/fuzzer_stats
		execs_per_sec=$( grep execs_per_sec zafl_out/fuzzer_stats )
		log_success "$queens_zafl: $execs_per_sec"
	else
		log_error "$queens_zafl: unable to run with afl"
	fi

}

build_all_exes()
{
    rm *.ncexe
    gcc -m64 -fno-stack-protector -O1 -std=c99 -o eightqueens_c_O1.ncexe $TEST_SRC_DIR/eightqueens.c
    if [ $? -ne 0 ]; then
        log_error "C build failure for O1 optimization level"
    fi

    gcc -m64 -fno-stack-protector -Og -std=c99 -o eightqueens_c_Og.ncexe $TEST_SRC_DIR/eightqueens.c
    if [ $? -ne 0 ]; then
        log_error "C build failure for Og optimization level"
    fi

    gcc -m64 -fno-stack-protector -O3 -std=c99 -o eightqueens_c_O3.ncexe $TEST_SRC_DIR/eightqueens.c
    if [ $? -ne 0 ]; then
        log_error "C build failure for O3 optimization level"
    fi

    g++ -m64 -fno-stack-protector -O1 -std=c++1y -o eightqueens_cpp_O1.ncexe $TEST_SRC_DIR/eightqueens.cpp
    if [ $? -ne 0 ]; then
        log_error "C++ build failure for O1 optimization level"
    fi

    g++ -m64 -fno-stack-protector -Og -std=c++1y -o eightqueens_cpp_Og.ncexe $TEST_SRC_DIR/eightqueens.cpp
    if [ $? -ne 0 ]; then
        log_error "C++ build failure for Og optimization level"
    fi

    g++ -m64 -fno-stack-protector -O3 -std=c++1y -o eightqueens_cpp_O3.ncexe $TEST_SRC_DIR/eightqueens.cpp
    if [ $? -ne 0 ]; then
        log_error "C++ build failure for O3 optimization level"
    fi

    log_success "All builds of exes succeeded."
}

test_one_exe()
{
    test_exe=$1
# build with graph optimization
    zafl.sh $test_exe $test_exe.stars.zafl.d.g.r.cs -d -g -c all --tempdir analysis.eightqueens.stars.zafl.d.g.r.cs -r 123 --enable-context-sensitivity function
    if [ $? -eq 0 ]; then
	     log_success "build $test_exe.stars.zafl.d.g.r.cs"
    else
	     log_error "build $test_exe.stars.zafl.d.g.r.cs"
    fi
    log_message "Fuzz for $AFL_TIMEOUT secs"
    fuzz_with_zafl $(realpath ./$test_exe.stars.zafl.d.g.r.cs)

# test functionality
    ./$test_exe > out.eightqueens.orig
    ./$test_exe.stars.zafl.d.g.r.cs > out.eightqueens.stars.zafl.d.g.r.cs
    diff out.eightqueens.orig out.eightqueens.stars.zafl.d.g.r.cs >/dev/null 2>&1
    if [ $? -eq 0 ]; then
	     log_success "$test_exe.stars.zafl.d.g.r.cs basic functionality"
    else
	     log_error "$test_exe.stars.zafl.d.g.r.cs basic functionality"
    fi
}

mkdir -p $session
pushd $session

build_all_exes

test_one_exe "eightqueens_c_O1.ncexe"

popd

cleanup
