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
```
cd $ZAFL_HOME
. set_env_vars
```

### Testing Zipr
Test that the binary rewriting infrastructure by rewriting /bin/ls
```
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



```
cd $ZAFL_HOME/zfuzz/test/gzip
./test_gzip.sh
```




