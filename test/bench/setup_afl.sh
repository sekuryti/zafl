tools="aflgcc zafl dyninst qemu"
binutils_binaries="size strings readelf strip-new nm-new"

for b in $binutils_binaries
do
	for t in $tools
	do
		pushd ${b}_${t}
		if [ ! -d in ]; then
			mkdir in
		fi
		echo "1" > in/1
		popd
	done
done
