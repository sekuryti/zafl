cd $(dirname $(realpath $0) )
PUT=test_mystrlen.exe
MYARG="0123456789abcdef"

ZAFL_PUT="$PUT.zafl $PUT.zafl.c $PUT.zafl.g $PUT.zafl.d $PUT.zafl.d.g $PUT.zafl.c.d.g"

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
	which afl-showmap
	if [ ! $? -eq 0 ]; then
		log_error "AFL doesn't seem to be installed. Try: sudo apt install afl before proceeding"
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

	build_one $PUT $PUT.zafl -v -t $PUT.analysis 
	build_one $PUT $PUT.zafl.c -c -v -t $PUT.analysis.c
	build_one $PUT $PUT.zafl.g -g -v -t $PUT.analysis.g
	build_one $PUT $PUT.zafl.d -d -v -t $PUT.analysis.d
	build_one $PUT $PUT.zafl.d.g -d -g -v -t $PUT.analysis.d.g
	build_one $PUT $PUT.zafl.c.d.g -d -g -v -t $PUT.analysis.c.d.g
}

clean_all()
{
	rm -fr ${PUT}* 
}

verify_output()
{
	./$PUT $MYARG TR > $PUT.output.orig

	for p in $ZAFL_PUT
	do
		echo "Program under test: $p"
		./${p} $MYARG > $p.output
		diff $PUT.output.orig $p.output
		if [ ! $? -eq 0 ]; then
			log_error "output verification failure: $p.output"
		fi

	done

	log_msg "output verified"
}

verify_afl_map()
{
	for p in $ZAFL_PUT
	do
		echo "Computing trace maps for input $MYARG"
		afl-showmap -o $p.map -- ./$p $MYARG
		cut -d':' -f2 $p.map | sort -r | head -n 1 > $p.max_count
	done

	for p in $ZAFL_PUT
	do
		diff $PUT.zafl.max_count $p.max_count >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			max=$(cat $PUT.zafl.max_count)
			log_msg "maximum edge counter for $PUT.zafl and $p match ($max)"
		else
			echo -n "Maximum count for $PUT: "
			cat $PUT.zafl.max_count
			echo -n "Maximum count for $p: "
			cat $p.max_count
			log_error "maximum edge counter does not match for $PUT.zafl and $p"
		fi
	done
}

clean_all
check_afl
build_all
verify_output
verify_afl_map
clean_all
