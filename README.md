# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs. 

Key features of Zafl:
* Uses Zipr, a fast, space-efficient binary rewriter to inline AFL-style instrumentations
* 100% AFL compatible
* Platform for experimenting with other instrumentation to guide afl, e.g., add calling context to the edge-profile function (a la Angora)

## Installation with Docker

A Docker-based installation is recommended for those getting started with Zafl.  
Please intending to develop new functionality or fix bugs in existing code should install from source (see next section).

To install the docker-based installation, please see the [install and use directions for the Docker-based setup.](https://git.zephyr-software.com/opensrc/libzafl/-/wikis/home)

## Installation from Source

Installing from source is not recommended for first-time Zafl users.  Please see the Docker-based setup instructions.
This method is recommended only for those intending to bug-fix or develop new features for Zafl.

The instructions that follow assume that:
* you are using a recent version of Linux, e.g., Ubuntu 18.04

### Install AFL locally
```bash
git clone https://github.com/google/AFL
```

Follow directions to build and install AFL

### Install the Zipr static binary rewriting infrastructure
See directions for this at https://git.zephyr-software.com/opensrc/zipr

### Testing Zipr
Test  the binary rewriting infrastructure by rewriting /bin/ls
```bash
cd /tmp
$PSZ /bin/ls ls.zipr -c rida
```

Your terminal's output should look like this:
```
Using Zipr backend.
Detected ELF file.
Performing step gather_libraries [dependencies=mandatory] ...Done. Successful.
Performing step meds_static [dependencies=mandatory] ...Done. Successful.
Performing step pdb_register [dependencies=mandatory] ...Done. Successful.
Performing step fill_in_cfg [dependencies=mandatory] ...Done. Successful.
Performing step fill_in_indtargs [dependencies=mandatory] ...Done. Successful.
Performing step clone [dependencies=mandatory] ...Done. Successful.
Performing step fix_calls [dependencies=mandatory] ...Done. Successful.
Program not detected in signature database.
Performing step zipr [dependencies=clone,fill_in_indtargs,fill_in_cfg,pdb_register] ...Done. Successful.
```

Invoke the rewritten version of /bin/ls and make sure it runs normally: 
```
./ls.zipr
``` 

### Download and Set environment for IRDB-SDK and IRDB-libs

Option 1:

If you built Zipr from source, you can use Zipr's set_env_vars feature to
include the right settings. 

Option 2:

Download the sdk:
```bash
git clone --recurse-submodules http://git.zephyr-software.com:opensrc/irdb-sdk.git
export IRDB_SDK=$PWD/irdb-sdk
```
Download the the built libraries for your system:
```
TBD
export IRDB_LIBS=/path/to/irdb-libs/libs
```


### Downloading and Building ZAFL
Once Zipr has been installed, clone the repo for ZAFL and build.
```bash
git clone --recurse-submodules http://git.zephyr-software.com:opensrc/zafl.git
# or: git clone --recurse-submodules git@git.zephyr-software.com:opensrc/zafl.git
cd zafl
```
Setup your environment:
```bash
. set_env_vars
```

And build zafl.
```bash
scons 
# or scons debug=1
# or scons -j3
# or scons debug=1 -j3
```

## Testing Zafl
Before running Zafl, always make sure to have the proper environment variables set
```bash
cd ~/zafl_umbrella
. set_env_vars
```

Zafl also needs to find Zipr, which can be done by setting:
```bash
export PSZ=/path/to/ps_zipr.sh

# or

export PATH=$PATH:$(dirname /path/to/ps_zipr.sh)
```

#### Running Zafl smoke tests
```bash
cd $ZAFL_HOME/test/bc
./test_bc.sh
```

The test will run afl on bc, instrumented with the proper instrumentation inlined. 
You will see several fuzzing run, each of which should take on the order of 30 seconds.

Once done, the output should end with something like:
```
unique_crashes    : 0
unique_hangs      : 0
last_path         : 0
last_crash        : 0
last_hang         : 0
execs_since_crash : 30242
exec_timeout      : 20
afl_banner        : bc
afl_version       : 2.52b
target_mode       : default
command_line      : afl-fuzz -i zafl_in -o zafl_out -- /usr/bin/bc
TEST PASS: /usr/bin/bc: execs_per_sec     : 1904.76
~/zafl_umbrella/test/bc
```

#### Final sanity check
```bash
cd /tmp
zafl.sh /bin/ls ls.zafl
```

**zafl.sh** is the primary script for adding afl instrumentation to binaries. 
You should see:
```
zafl.sh /bin/ls ls.zafl
Zafl: Transforming input binary /bin/ls into ls.zafl
Zafl: Issuing command: /home/zafl_guest/zafl_umbrella/install/zipr_umbrella/peasoup_examples/tools/ps_zipr.sh /bin/ls ls.zafl -c move_globals=on -c zafl=on -o move_globals:--elftables -o zipr:--traceplacement:on -o zipr:true -o zafl:--stars 
Using Zipr backend.
Detected ELF file.
Performing step gather_libraries [dependencies=mandatory] ...Done. Successful.
Performing step meds_static [dependencies=mandatory] ...Done. Successful.
Performing step pdb_register [dependencies=mandatory] ...Done. Successful.
Performing step fill_in_cfg [dependencies=mandatory] ...Done. Successful.
Performing step fill_in_indtargs [dependencies=mandatory] ...Done. Successful.
Performing step clone [dependencies=mandatory] ...Done. Successful.
Performing step fix_calls [dependencies=mandatory] ...Done. Successful.
Program not detected in signature database.
Performing step move_globals [dependencies=none] ...Done. Successful.
Performing step zafl [dependencies=none] ...Done. Successful.
Performing step zipr [dependencies=clone,fill_in_indtargs,fill_in_cfg,pdb_register] ...Done. Successful.
```

You can run **ls.zafl** as you would **ls**: ```./ls.zafl```

Zafl'd binaries can be run normally. There is no extra output.

To make sure the binary has been instrumented properly: ```ZAFL_DEBUG=1 ./ls.zafl```

The output should start with:
```
Error getting shm environment variable - fake allocate AFL trace map
Success at mmap!
libautozafl: auto-initialize fork server
```
Let's prep for fuzzing:
```bash
mkdir input_seeds
echo "hello" > input_seeds/hello.seed
```

Let's now run the Zafl'd binary with afl:
```bash
afl-fuzz -i input_seeds -o out -- ./ls.zafl @@
```

If afl complains about `missing instrumentation`, you'll need to set the following environment variable:
```bash
export AFL_SKIP_BIN_CHECK=1
```

You can also run the usual afl utilities, e.g:
```bash
afl-showmap -o map.out -- ./ls.zafl
afl-cmin -i out/queue/ -o out.cmin -- ./ls.zafl @@
```

Et voila!

# TL;DR
Once everything is installed properly, you can prep a binary for fuzzing with the simple command:
```bash
zafl.sh <target_binary> <zafl_output_binary>
```




