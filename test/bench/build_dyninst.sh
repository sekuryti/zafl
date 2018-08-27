source binutils.spec
echo "Build dyninst version of $binutils_binaries"

AFL_DYNINST_DIR=/home/an7s/aware/zfuzz/afl-dyninst/
AFL_DYNINST=$AFL_DYNINST_DIR/afl-dyninst

for b in $binutils_binaries
do
	dyninst_dir="${b}_dyninst"
	if [ ! -d $dyninst_dir ];
	then
		mkdir ${b}_dyninst
	fi

	cp binutils-gdb/binutils/$b ${b}_dyninst

	pushd $dyninst_dir
	echo "Building Zafl version of $b"
	ln -s $AFL_DYNINST_DIR/libAflDyninst.so .
	$AFL_DYNINST -f -i $b -o ${b}.dyninst
	mkdir in
	echo "1" > in/1
	popd
done
