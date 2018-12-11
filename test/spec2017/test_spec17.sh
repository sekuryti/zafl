#!/bin/bash

# the bad boys

run_size="ref"

all_benchmarks=" 
600.perlbench_s 602.gcc_s 603.bwaves_s 605.mcf_s 607.cactuBSSN_s 619.lbm_s 620.omnetpp_s 621.wrf_s 623.xalancbmk_s 625.x264_s 627.cam4_s 628.pop2_s 631.deepsjeng_s 638.imagick_s 641.leela_s 644.nab_s 648.exchange2_s 649.fotonik3d_s 654.roms_s 657.xz_s 996.specrand_fs 998.specrand_is 
"

#all_benchmarks="
#500.perlbench_r
#"



number=1

setup()
{

	if [ ! -d spec2017 ]; then
		#svn co ^/spec2017/trunk spec2017
		git clone --depth 1 http://git.zephyr-software.com/allzp/spec2017.git spec2017
	fi

	if [[ ! -f /usr/bin/gfortran ]]; then
		sudo apt-get install gfortran gcc g++ -y
	fi

	cd spec2017/
	source shrc
}


run_test()
{
	local config_name="$1"
	local config="$2"
	local benchmarks="$3"

	tests_that_ran="$tests_that_ran $config_name"

	cd $SPEC
	if [ ! -d result.$config_name ]; then
		bash -x $PEASOUP_UMBRELLA_DIR/postgres_reset.sh
		rm -Rf result/*
		runcpu  --action scrub --config $config $benchmarks

		echo
		echo "**************************************************************************"
		echo "Starting test of $config_name"
		echo "**************************************************************************"
		echo
		#runspec  --action validate --config $config -n $number $benchmarks 
                runcpu  --config $config --iterations $number --size $run_size --copies=8 --parallel_test_workload $run_size --noreportable $benchmarks

 		cp benchspec/CPU/*/exe/* result
		mv result result.$config_name
		for bench in $benchmarks
		do
                        mv benchspec/CPU/$bench/build/build*/peasoup*/logs result.$config_name/$bench.log
		done
	fi

}

get_size_result()
{
	bench=$1
	if [ -e $bench ]; then
		size=$(stat --printf="%s" $bench)
		#echo -n "$size"
		#LC_ALL= numfmt --grouping $size
		#LC_ALL= printf "%'d" $size
		#LC_NUMERIC=en_US printf "%'d" $size
		#LC_NUMERIC=en_US printf "%'f" $size
		#LC_NUMERIC=en_US printf "%'.f" $size
		#LC_NUMERIC=en_US printf "%'10.10f" $size
		#LC_NUMERIC=en_US /usr/bin/printf "%'d" $size
		echo $size
	else
		echo -n "0"
	fi
}

get_result()
{
	bench=$1
	config=$2

	results=$(cat $SPEC/result.$config/CPU2017.002.log|grep Success|grep $bench|grep ratio=|sed 's/.*ratio=//'|sed 's/,.*//')

	sum=0
	count=0
	for res in $results
	do
		sum=$(echo $sum + $res | bc)
		count=$(echo $count + 1  | bc)
	done
	#echo sum=$sum
	#echo count=$count
	res=$(echo  "scale=2; $sum / $count" | bc 2> /dev/null )

	count=$(echo $res|wc -w)

	if [ $count = 1 ];  then
		echo -n $res
	else
		echo -n "0"
	fi

}


get_raw_results()
{
	get_raw_perf_results $tests_that_ran
	get_raw_size_results $tests_that_ran
	#get_raw_fde_results $tests_that_ran
}

get_raw_perf_results()
{
	configs="$*"
	first_config=$1


	echo "--------------------------------------------------------------"
	echo "Performance results are:"
	echo "--------------------------------------------------------------"
	echo benchmark $configs
	for bench in $all_benchmarks
	do
		echo -n "$bench 	"
		for config in $*
		do
			get_result $bench $config
			echo -n "	"
		done
		echo
	done
}

get_raw_size_results()
{
	echo "--------------------------------------------------------------"
	echo "Size results are:"
	echo "--------------------------------------------------------------"
	configs=$*
	echo benchmark $configs
	for bench in $SPEC/result.$first_config/*mytest-m64
	do
		echo -n "$(basename $bench _base.mytest-m64)	"
		for config in $*
		do
			if [[ $config == "baseline" ]]; then
				file="$SPEC/result.$config/$(basename $bench)"
				cp $file /tmp/foo.exe
				strip /tmp/foo.exe
				file="/tmp/foo.exe"
			else
				file="$SPEC/result.$config/$(basename $bench)"
			fi
			res=$(get_size_result $file)

			#printf "%15s" $res
			echo -n "	$res"
		done
		echo
	done

}

get_raw_fde_results()
{
	echo "--------------------------------------------------------------"
	echo "FDE results are:"
	echo "--------------------------------------------------------------"
	configs=$*
	echo benchmark $configs
	for bench in $SPEC/result.$first_config/*mytest-m64
	do
		#printf "%-20s"  $(basename $bench _base.amd64-m64-gcc42-nn)
		echo -n $(basename $bench _base.amd64-m64-gcc42-nn)
		for config in $*
		do
			file="$SPEC/result.$config/$(basename $bench)"
			res=$(readelf -w $file |grep FDE|wc -l )
			#if [[ $config == "baseline" ]]; then
			#else
			#fi

			#printf "%15s" $res
			echo -n "	$res"
		done
		echo
	done

}

main()
{
	local zipr_flags="	--backend zipr --step-option zipr:--add-sections --step-option zipr:true"
	local trace_flags="   --step-option zipr:--traceplacement:on --step-option zipr:true"
	local relax_flags="   --step-option zipr:--relax:on --step-option zipr:true --step-option zipr:--unpin:on --step-option zipr:false"
	local nounpin_flags=" --step-option zipr:--unpin:on --step-option zipr:false"
	local split_flags="   --step-option fill_in_indtargs:--split-eh-frame "
	local icall_flags="   --step-option fix_calls:--no-fix-icalls "
	local p1flags=" 	-c p1transform=on " 
	local zafl_flags="    --backend zipr -s meds_static=off -s rida=on -c move_globals=on -c zax=on -o move_globals:--elftables-only "
	local zafl_opt_flags="--backend zipr -s meds_static=off -s rida=on -c move_globals=on -c zax=on -o move_globals:--elftables-only -o zipr:--traceplacement:on -o zax:--stars "

	# sets $SPEC
	setup

	local nops_config=$SPEC/config/ubuntu14.cfg
	local withps_config=$SPEC/config/ubuntu14_withps.cfg
	

	# baseline 
	run_test baseline $SPEC/config/ubuntu14.cfg "$all_benchmarks"

	PSOPTS="$zipr_flags "  run_test zipr  $withps_config "$all_benchmarks"

	PSOPTS="$zafl_flags "  run_test zafl  $withps_config "$all_benchmarks"

	get_raw_results 
}

main "$@"



