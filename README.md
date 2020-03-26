# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs. 

Key features of Zafl:
* Uses Zipr, a fast, space-efficient binary rewriter to inline AFL-style instrumentations
* 100% AFL compatible
* Platform for experimenting with other instrumentation to guide afl, e.g., add calling context to the edge-profile function (a la Angora)

## Installation
The instructions that follow assume that:
* you have `sudo` privileges
* you are installing in your home directory

### You will first need to install the Zipr static binary rewriting infrastructure
```bash
cd ~
git clone --recurse-submodules git@git.zephyr-software.com:allnp/peasoup_umbrella.git
cd peasoup_umbrella
. set_env_vars
./get-packages.sh
scons -j3
```

### Setting up local postgres tables
Next we need to setup the proper tables in a local copy of the postgres database.
```bash
cd ~/peasoup_umbrella
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

### Installing ZAFL
Once Zipr has been installed, clone the repo for ZAFL and build.
```bash
git clone --recurse-submodules git@git.zephyr-software.com:allnp/zafl_umbrella.git
cd zafl_umbrella
. set_env_vars
scons -j3
```

## Testing Zafl
Before running Zafl, always make sure to have the proper environment variables set
```bash
cd ~/peasoup_umbrella
. set_env_vars

cd ~/zafl_umbrella
. set_env_vars
```

#### Running Zafl smoke tests
```bash
cd $ZAFL_HOME/zfuzz/test/bc
./test_bc.sh
```

The test will run afl on bc, instrumented with the proper instrumentation inlined. 
The output should end with:
```
execs_since_crash : 77855
exec_timeout      : 20
afl_banner        : bc.stars.zafl
afl_version       : 2.52b
target_mode       : default
command_line      : afl-fuzz -i zafl_in -o zafl_out -- ./bc.stars.zafl -f
TEST PASS: ./bc.stars.zafl: ran zafl binary: execs_per_sec     : 2000.00
TEST PASS: all tests passed: zafl instrumentation operational on bc
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
Error getting shm environment variable - fake allocate AFL trace map
zafl_initAflForkServer(): Bad file descriptor
```

Let's now run the Zafl'd binary with afl:
```bash
afl-fuzz -i in -o out -- ./ls.zafl @@
```

You can also run the usual afl utilities, e.g:
```bash
afl-showmap -o map.out -- ./ls.zafl
afl-cmin -i out/queue/ -o out.cmin -- ./ls.zafl @@
```

Et voila!

# TL;DR
Once everything is installed properly, you can prep a binary for fuzzing with the simple command:
```zafl.sh <target_binary> <zafl_output_binary>```




