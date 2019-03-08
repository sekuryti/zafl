SCRIPT=$(readlink -f $0)                                                                            

cd $(dirname $SCRIPT)
source binutils.spec

echo "Build afl-gcc version of $binutils_binaries"

for b in $binutils_binaries
do
	aflgcc_dir="${b}_aflgcc"
	if [ ! -d $aflgcc_dir ];
	then
		mkdir ${b}_aflgcc
	fi

	cp binutils-2.30/binutils/$b ${b}_aflgcc/${b}.aflgcc
done
