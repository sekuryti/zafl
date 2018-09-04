# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs. 

Key features of Zafl:
* Uses Zipr, a fast, space-efficient binary rewriter to inline AFL-style instrumentations. Preliminary overhead: 
    * 20% slower than afl/source code
    * 15% faster than afl/dyninst
    * a **lot** faster than afl/QEMU
* Platform for experimenting with other edge-profile instrumentation, e.g., add calling context to the edge-profile function (a la Angora)

## Installation
Note that you will need **sudo** privileges to get and install all the required packages.

### Getting packages and compiling Zafl
```bash
git clone --recursive git@git.zephyr-software.com:allnp/zafl_umbrella.git
cd zafl_umbrella
. set_env_vars
./get_packages.sh
./build-all.sh
```
Note:
* Zafl automatically downloads and builds AFL and AFL/QEMU
* Building Zafl takes approximately 15 minutes

### Setting up local postgres database
Next we need to setup a local copy of the postgres database
```bash
cd $ZAFL_HOME/zipr_umbrella
./postgres_setup.sh
```

If all goes well with the postgres setup, you should be able to login into the database by typing: ```psql``` 

### Setting up IDA with your license key
...

## Testing Zafl

### Testing Zipr
Test that the binary rewriting infrastructure by rewriting say /bin/ls
```$PSZ /bin/ls ls.zipr```

Invoke the rewritten version of /bin/ls and make sure it runs normally: ```./ls.zipr``` 

### Testing Zafl
```
cd $ZAFL_HOME/zfuzz/test/gzip
./test_gzip.sh
```
