pushd binutils-2.30
make clean distclean
rm -fr config.cache
rm -fr */config.cache
rm -fr */*/config.cache
CC=afl-gcc ./configure
make clean all
popd
