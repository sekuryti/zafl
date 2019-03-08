pushd binutils-2.30
make clean distclean
rm -fr config.cache
rm -fr */config.cache
rm -fr */*/config.cache
./configure
make clean all
popd
