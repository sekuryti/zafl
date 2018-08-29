tools="aflgcc zafl dyninst qemu"

#binutils_binaries="size strings readelf objdump cxxfilt ar"
binutils_binaries="objdump size readelf strings cxxfilt nm-new strip-new ar"

# specify how to run under afl
declare -A fuzz_map
fuzz_map["size"]="@@"
fuzz_map["objdump"]="-d @@"
fuzz_map["readelf"]="-a @@"
fuzz_map["strings"]=""
fuzz_map["cxxfilt"]=""
fuzz_map["nm-new"]="-a @@"
fuzz_map["strip-new"]="@@"

# additional afl params to pass in
declare -A afl_args
afl_args["objdump"]=" -m 150 "
