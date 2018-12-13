ZAFL installation

============================
=== ZAFL
============================

1. Install the tarball locally on your machine

	tar -xzvf zafl_install.tgz

2. Set the proper environment variables. To use any Zipr-based tools, you will
need these environment variables to be set.

	cd zafl_install
	. set_env_vars

3. You will need to have sudo priviledges (please don't run as root)

4. Make sure the postgres DB is running and that you have an account

	cd $ZAFL_HOME/zipr_umbrella
	./postgres_setup.sh

============================
=== Zafl binary fuzzing  ===
============================

1. Again make sure the proper environment variables are set

	cd zafl_install
	. set_env_vars

2. Test Zafl instrumentation

	pushd /tmp

	zafl.sh `which bc` bc.zafl
	ldd bc.zafl
	# you should see lizafl.so as a dependence

	ZAFL_DEBUG=1 ./bc.zafl
	# you should see output of the form (this is normal)
	Error getting shm environment variable - fake allocate AFL trace map

	# at this point, you should be able to use bc normally
	at the bc promt, enter: 2+3 
	the answer should be: 5

	popd

3. Download and install your own version of American Fuzzy Lop (afl)

	Be sure to set the environment variable AFL_PATH to point to your afl installation directory

	After installing, type:
		afl-fuzz 

	You should see the usage instructions for afl-fuzz

4. Test with bc.zafl created in step 2

	pushd /tmp

	mkdir input_seeds
	echo "1" > input_seeds/seed.1

	afl-fuzz -i input_seeds -o afl_output -- ./bc.zafl

	# afl should now be fuzzing bc.zafl
	
	popd


