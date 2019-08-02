cd $(dirname $(realpath $0) )

PUT=test_context.exe
ZAFL_PUT="$PUT.zafl $PUT.zafl.context_sensitive"
MYARG="a"

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
	g++ test_context.c -o $PUT
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

zafl_map=${PUT}.zafl.map
afl-showmap -o $zafl_map -- ./${PUT}.zafl
count_zafl=$(wc -l ${zafl_map} | cut -d' ' -f1)

zafl_cs_map=${PUT}.zafl.map
afl-showmap -o $zafl_cs_map -- ./${PUT}.zafl.context_sensitive
count_zafl_cs=$(wc -l ${zafl_cs_map} | cut -d' ' -f1)

# difference must be exactly 2
let diff=$count_zafl_cs-$count_zafl
if [ $diff -eq 2 ]; then
	log_msg "context sensitive map has +2 entries over baseline zafl map"
else
	log_error "context sensitive map does not have expected number of entries (should be +2): map_size(zafl):$count_zafl map_size(zafl_context_sensitive):$count_zafl_cs"
fi

clean_all
