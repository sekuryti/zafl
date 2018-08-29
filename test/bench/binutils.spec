#tools can be aflgcc zafl qemu dyninst
tools="aflgcc zafl qemu"

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
fuzz_map["gzip"]="-f"

# additional afl params to pass in
declare -A afl_args
afl_args["objdump"]=" -m 150 "
