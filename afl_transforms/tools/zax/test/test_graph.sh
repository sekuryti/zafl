cd $(dirname $(realpath $0) )

PUT=test_mystrlen.exe
MYARG="0123456789abcdef"
PUT2=test_mystrlen2.exe
MYARG2="0123456789abcdef12345sdafdasjfadsjk"

ZAFL_PUT="$PUT.zafl $PUT.zafl.c $PUT.zafl.g $PUT.zafl.d $PUT.zafl.d.g $PUT.zafl.c.d.g"
ZAFL_PUT2="$PUT2.zafl $PUT2.zafl.c $PUT2.zafl.g $PUT2.zafl.d $PUT2.zafl.d.g $PUT2.zafl.c.d.g"

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
	orig=$1
	zafl=$2
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
	g++ test_mystrlen.cpp -o $PUT
	g++ test_mystrlen2.cpp -o $PUT2
}

zafl_all()
{
	for p in $*
	do
		build_one $p $p.zafl -v -t $p.analysis 
		build_one $p $p.zafl.c -c -v -t $p.analysis.c
		build_one $p $p.zafl.g -g -v -t $p.analysis.g
		build_one $p $p.zafl.d -d -v -t $p.analysis.d
		build_one $p $p.zafl.d.g -d -g -v -t $p.analysis.d.g
		build_one $p $p.zafl.c.d.g -d -g -v -t $p.analysis.c.d.g
	done
}

clean_all()
{
	rm -fr ${PUT}* ${PUT2}*
}

verify_output()
{
	orig_zafl=$1
	shift
	all_configs=$*

	./$orig_zafl $MYARG > $orig_zafl.output.orig

	for p in $all_configs 
	do
		echo "Program under test: $p"
		./${p} $MYARG > $p.output
		diff $orig_zafl.output.orig $p.output
		if [ ! $? -eq 0 ]; then
			log_error "output verification failure: $p.output"
		fi
	done

	log_msg "output verified for $orig_zafl"
}

verify_afl_map()
{
	arg=$1
	shift
	orig_zafl=$1
	shift
	all_configs=$*
	for p in $all_configs
	do
		echo "Computing trace maps for input $MYARG"
		afl-showmap -o $p.map -- ./$p $arg
		cut -d':' -f2 $p.map | sort -r | head -n 1 > $p.max_count
	done

	for p in $all_configs
	do
		diff $orig_zafl.zafl.max_count $p.max_count >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			max=$(cat $orig_zafl.zafl.max_count)
			log_msg "maximum afl edge value for $orig_zafl.zafl and $p match ($max)"
		else
			echo -n "Maximum count for $orig_zafl: "
			cat $orig_zafl.zafl.max_count
			echo -n "Maximum count for $p: "
			cat $p.max_count
			log_error "maximum afl edge value does not match for $orig_zafl.zafl and $p"
		fi
	done
}

clean_all
check_afl

build_all

zafl_all $PUT
verify_output $PUT $ZAFL_PUT
verify_afl_map $MYARG $PUT $ZAFL_PUT

zafl_all $PUT2
verify_output $PUT2 $ZAFL_PUT2
verify_afl_map $MYARG2 $PUT2 $ZAFL_PUT2

clean_all
