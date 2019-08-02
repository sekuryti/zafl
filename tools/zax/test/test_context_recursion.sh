cd $(dirname $(realpath $0) )

PUT=test_fib.exe
ZAFL_PUT="$PUT.zafl $PUT.zafl.context_sensitive"
MYARG=8

log_msg()
{
	echo "TEST PASS: $1"
}

log_error()
{
	echo "TEST FAIL: $1"
	exit 1
}

check_afl()
{
	which afl-showmap >/dev/null 2>&1
	if [ ! $? -eq 0 ]; then
		log_error "AFL doesn't seem to be installed. Try: 'sudo apt install afl' before proceeding or download/build afl directly from source"
	fi
}

build_one()
{
	local orig=$1
	local zafl=$2
	shift
	shift
	zafl.sh $orig $zafl $@
	if [ $? -eq 0 ]; then
		log_msg "build $zafl" 
	else
		log_error "build $zafl" 
	fi
}

build_all()
{
	g++ test_fib.c -o $PUT
}

zafl_all()
{
	for p in $*
	do
		build_one $p $p.zafl -v -t $p.analysis 
		build_one $p $p.zafl.context_sensitive --enable-context-sensitivity function -v -t $p.analysis.context_sensitive
	done
}

clean_all()
{
	rm -fr ${PUT}* 
}

verify_output()
{
	local arg=$1
	shift
	local orig_zafl=$1
	shift
	local all_configs=$*

	timeout 30 ./$orig_zafl $arg > $orig_zafl.output.orig

	for p in $all_configs 
	do
		echo "Program under test: $p"
		timeout 30 ./${p} $arg > $p.output
		diff $orig_zafl.output.orig $p.output
		if [ ! $? -eq 0 ]; then
			log_error "output verification failure: $p.output"
		fi
	done

	log_msg "output verified for $orig_zafl"
}

clean_all
check_afl

build_all

zafl_all $PUT
verify_output $MYARG $PUT $ZAFL_PUT

zafl_map15=${PUT}.zafl.map
afl-showmap -o $zafl_map15 -- ./${PUT}.zafl 15
count_zafl15=$(wc -l ${zafl_map15} | cut -d' ' -f1)

zafl_cs_map8=${PUT}.zafl.cs.map.8
afl-showmap -o $zafl_cs_map8 -- ./${PUT}.zafl.context_sensitive 8
count_zafl_cs8=$(wc -l ${zafl_cs_map8} | cut -d' ' -f1)

zafl_cs_map20=${PUT}.zafl.cs.map.20
afl-showmap -o $zafl_cs_map20 -- ./${PUT}.zafl.context_sensitive 20
count_zafl_cs20=$(wc -l ${zafl_cs_map20} | cut -d' ' -f1)

# make sure we have more entries with context sensitivity
let diff=$count_zafl_cs8-$count_zafl15
if [ $diff -gt 5 ]; then
	log_msg "vanilla zafl with deep recursion should have fewer entries than context sensitive version"
else
	log_error "vanilla zafl with deep recursion should have fewer entries than context sensitive version"
fi

# make sure we don't blow out the map with deep recursion
let diff=$count_zafl_cs20-$count_zafl_cs8
if [ $diff -eq 0 ]; then
	log_msg "recursion level of 8 or 20 should have same number of entries in the trace map"
else
	log_error "recursion level of 8 or 20 should have same number of entries in the trace map"
fi

clean_all
