pushd binutils-gdb
make clean distclean
rm -fr config.cache
rm -fr */config.cache
rm -fr */*/config.cache
./configure
make clean all
popd
