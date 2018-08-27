source binutils.spec
echo "Build qemu version of $binutils_binaries"

for b in $binutils_binaries
do
	qemu_dir="${b}_qemu"
	if [ ! -d $qemu_dir ];
	then
		mkdir ${b}_qemu
	fi

	cp binutils-gdb/binutils/$b ${b}_qemu/${b}.qemu
done
