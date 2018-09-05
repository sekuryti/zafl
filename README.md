# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs. 

Key features of Zafl:
* Uses Zipr, a fast, space-efficient binary rewriter to inline AFL-style instrumentations. Preliminary overhead: 
    * 20% slower than afl/source code
    * 15% faster than afl/dyninst
    * a **lot** faster than afl/QEMU
* Platform for experimenting with other instrumentation to guide afl, e.g., add calling context to the edge-profile function (a la Angora)

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
The output of psql should look something like this:
```
psql (9.3.22)
SSL connection (cipher: DHE-RSA-AES256-GCM-SHA384, bits: 256)
Type "help" for help.

peasoup_XXX=> 
```
### Setting up IDA with your license key
The standard Zipr toolchain configuration uses IDA Pro as part of its analysis phases. 
Once you get a license from IDA, put your license key file in: ```$IDAROOT/```

Then you must run IDA once in interactive mode and accept the licensing terms: 
```
cd $IDAROOT
./idat64
```

## Testing Zafl

Before running Zafl, always make sure to have your environment variable set
```bash
cd $ZAFL_HOME
. set_env_vars
```

### Testing Zipr
Test that the binary rewriting infrastructure by rewriting /bin/ls
```bash
cd /tmp
$PSZ /bin/ls ls.zipr
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

### Testing Zafl
#### Make sure afl itself is setup properly
```
cd /tmp
mkdir in
echo "1" > in/1
afl-fuzz -i in -o out -Q -- /bin/ls @@
```

You may see afl error messages such as this one that will need to be fixed:
```
afl-fuzz 2.52b by <lcamtuf@google.com>
[+] You have 24 CPU cores and 1 runnable tasks (utilization: 4%).
[+] Try parallel jobs - see docs/parallel_fuzzing.txt.
[*] Checking CPU core loadout...
[+] Found a free CPU core, binding to #0.
[*] Checking core_pattern...

[-] Hmm, your system is configured to send core dump notifications to an
    external utility. This will cause issues: there will be an extended delay
    between stumbling upon a crash and having this information relayed to the
    fuzzer via the standard waitpid() API.

    To avoid having crashes misinterpreted as timeouts, please log in as root
    and temporarily modify /proc/sys/kernel/core_pattern, like so:

    echo core >/proc/sys/kernel/core_pattern

[-] PROGRAM ABORT : Pipe at the beginning of 'core_pattern'
         Location : check_crash_handling(), afl-fuzz.c:7275
```

or:

```
[-] Whoops, your system uses on-demand CPU frequency scaling, adjusted
    between 1558 and 2338 MHz. Unfortunately, the scaling algorithm in the
    kernel is imperfect and can miss the short-lived processes spawned by
    afl-fuzz. To keep things moving, run these commands as root:

    cd /sys/devices/system/cpu
    echo performance | tee cpu*/cpufreq/scaling_governor

    You can later go back to the original state by replacing 'performance' with
    'ondemand'. If you don't want to change the settings, set AFL_SKIP_CPUFREQ
    to make afl-fuzz skip this check - but expect some performance drop.
```

Fix any afl-related errors until you can run:
```afl-fuzz -i in -o out -Q -- /bin/ls @@```

#### Running Zafl smoke tests
```bash
cd $ZAFL_HOME/zfuzz/test/gzip
./test_gzip.sh
```

The test will run afl on gzip instrumented with the proper instrumentation inlined. 
The output should end with:
```
execs_since_crash : 77855
exec_timeout      : 20
afl_banner        : gzip.stars.zafl
afl_version       : 2.52b
target_mode       : default
command_line      : afl-fuzz -i zafl_in -o zafl_out -- ./gzip.stars.zafl -f
TEST PASS: ./gzip.stars.zafl: ran zafl binary: execs_per_sec     : 2000.00
TEST PASS: all tests passed: zafl instrumentation operational on gzip
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

You can run **ls.zafl** as you would ls: ```./ls.zafl```
Zafl'd binaries can be run normally. There is no extra output.

To make sure the binary has been instrumented properly: ```ZAFL_DEBUG=1 ./ls.zafl```

The output should start with:
```
Error getting shm environment variable - fake allocate AFL trace map
Error getting shm environment variable - fake allocate AFL trace map
zafl_initAflForkServer(): Bad file descriptor
```

Let's now run the Zafl'd binary with afl:
```
afl-fuzz -i in -o out -- ./ls.zafl @@
```

You can also run the usual afl utities, e.g:
```
afl-showmap -o map.out -- ./ls.zafl
afl-cmin -i out/queue/ -o out.cmin -- ./ls.zafl @@
```

Et voila!

# TL;DR
```zafl.sh <target_binary> <zafl_output_binary>```




