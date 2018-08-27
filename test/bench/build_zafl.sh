SCRIPT=$(readlink -f $0)                                                                            

cd $(dirname $SCRIPT)
source binutils.spec

echo "Zafl $binutils_binaries"

for b in $binutils_binaries
do
	zafl_dir="${b}_zafl"
	if [ ! -d $zafl_dir ];
	then
		mkdir ${b}_zafl
	fi

	cp binutils-gdb/binutils/$b ${b}_zafl/

	pushd $zafl_dir
	echo "Remove any remnants of previous analysis runs"
	rm -fr peasoup_exec*
	echo "Building Zafl version of $b"
	zafl.sh ./$b ${b}.zafl 

	ln -s $SECURITY_TRANSFORMS_HOME/lib/libzafl.so .

	if [ ! -d in ]; then
		mkdir in
	fi
	echo "1" > in/1
	popd
done
