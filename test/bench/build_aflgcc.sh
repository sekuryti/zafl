#binutils_binaries="size strings readelf strip-new nm-new"
binutils_binaries="size strings readelf objdump cxxfilt ar"
echo "Build afl-gcc version of $binutils_binaries"

for b in $binutils_binaries
do
	aflgcc_dir="${b}_aflgcc"
	if [ ! -d $aflgcc_dir ];
	then
		mkdir ${b}_aflgcc
	fi

	cp binutils-gdb/binutils/$b ${b}_aflgcc/${b}.aflgcc

	pushd $aflgcc_dir
	echo "Building Zafl version of $b"
	mkdir in
	echo "1" > in/1
	popd
done
