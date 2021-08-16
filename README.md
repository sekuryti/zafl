# ZAFL: Zipr-based AFL
Welcome to **ZAFL**: a project to extend compiler-quality instrumentation speed *and* transformation support to the fuzzing of x86-64 binary programs. The key features of ZAFL include:
* Fast, space-efficient, and inlined binary fuzzing instrumentation via the Zipr binary rewriting infrastructure.
* A platform to extend and combine compiler-style code transformations (e.g., CMP unfolding) to binary-only fuzzing.
* Full compatibility with the AFL and AFLPlusPlus fuzzer ecosystem.

<table><tr><td align=center colspan="2"><div><b>Presented in our paper</b> <a href="https://www.usenix.org/conference/usenixsecurity21/presentation/nagy"><i>Breaking-through Binaries: Compiler-quality Instrumentation for Better Binary-only Fuzzing</i></a><br>(To appear in the 2021 USENIX Security Symposium).</td </tr>
  <tr><td><b>Citing this repository:</b></td>
  <td><code class="rich-diff-level-one">@inproceedings{nagy:breakingthrough, title = {Breaking Through Binaries: Compiler-quality Instrumentation for Better Binary-only Fuzzing}, author = {Stefan Nagy and Anh Nguyen-Tuong and Jason D. Hiser and Jack W. Davidson and Matthew Hicks}, booktitle = {{USENIX} Security Symposium (USENIX)}, year = {2021},}</code></td></tr>
  <tr><td><b>License:</b></td><td><a href="https://git.zephyr-software.com/opensrc/zafl/-/blob/master/LICENSE">BSD 3-Clause License</a></td></tr>
  <tr><td><b>Disclaimer:</b></td><td><i>This software is provided as-is with no warranty.</i></td></tr></table>

## Prerequisites
* **Install environment**: ZAFL's installation currently supports recent 64-bit Linux environments (i.e., Ubuntu 16.04 and up). 
* **Supported binaries**: ZAFL supports instrumenting/transforming x86-64 Linux binaries of varying type (C and C++, stripped and unstripped, and position independent and position-non-independent). At this time, ZAFL cannot support binaries with DRM, obfuscation, or tamper-resistant protections.
* **Windows binaries**: ZAFL offers preliminary cross-platform instrumentation support for Windows 7 PE32+ binaries, though WinAFL-compatible fuzzing instrumentation is not yet supported at this time.
* **Recommended installation**: For first-time users we recommend the *Docker-based* installation. For developers and advanced users we recommend the *source-based* installation.

## Installation from Docker (recommended)
To install the Docker-based installation, please see the [install and use directions for the Docker-based setup.](https://git.zephyr-software.com/opensrc/libzafl/-/wikis/home)

## Installation from Source (for developers)
Installing from source is recommended only for those intending to bug-fix or develop new features for ZAFL. **The following instructions assume you are running a compatible Linux environment** (i.e., Ubuntu 16.04 and up) **with AFL installed**.

#### Step 0: Install the Zipr binary rewriting infrastructure
See https://git.zephyr-software.com/opensrc/zipr.
Before continuing, be sure to prepare Zipr's environment by doing the following:
```bash
cd /path/to/zipr && . set_env_vars
```

#### Step 1: Testing Zipr
Test the binary rewriting infrastructure by rewriting Linux's `ls` binary:
```bash
$PSZ /bin/ls /tmp/ls.zipr -c rida
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
Invoke the rewritten version and make sure it runs normally: 
```
./tmp/ls.zipr
``` 

#### Step 2: Download and build ZAFL
First, clone ZAFL's repository:
```bash
git clone --recurse-submodules https://git.zephyr-software.com/opensrc/zafl.git
```
Second, prepare ZAFL's environment:
```bash
cd /path/to/zafl && . set_env_vars
```
Lastly, build ZAFL:
```bash
scons 
# or scons debug=1
# or scons -j3
# or scons debug=1 -j3
```

## Using ZAFL for Linux Binary Fuzzing
Before running ZAFL, **always make sure to prepare both it and Zipr's environments**:
```bash
cd /path/to/zipr && . set_env_vars
cd /path/to/zafl && . set_env_vars
```

#### Step 0: Ensure ZAFL's smoke tests succeed
```bash
cd /path/to/zafl/test/bc && ./test_bc.sh
```
This test will instrument and fuzz a copy of Linux's `bc` binary. If successful, the final output should contain something like:
```bash
command_line      : afl-fuzz -i zafl_in -o zafl_out -- ./bc.zafl
TEST PASS: ./bc.zafl: execs_per_sec     : 1904.76
```

#### Step 1: Instrument your binary
**zafl.sh** is the primary script for instrumenting/transforming binaries for fuzzing. Below presents an example of running it on the `ls` binary
```bash
zafl.sh /bin/ls /tmp/ls.zafl
```
The output should contain the following:
```
Zafl: main exec is PIE... use entry point address (0x5850) for fork server 
Zafl: Transforming input binary /bin/ls into ./tmp/ls.zafl 
Zafl: Issuing command:  ~/zipr/installed/tools/ps_zipr.sh /bin/ls ./tmp/ls.zafl  -c rida  -s move_globals  -c zax -o move_globals:--elftables-only -o move_globals:--no-use-stars  -o zax:--stars     -o zax:--enable-floating-instrumentation       -o zax:'-e 0x5850'     
Using Zipr backend.
Detected ELF shared object.
Performing step rida [dependencies=mandatory] ...Done.  Successful.
Performing step pdb_register [dependencies=mandatory] ...Done.  Successful.
Performing step fill_in_cfg [dependencies=unknown] ...Done.  Successful.
Performing step fill_in_indtargs [dependencies=unknown] ...Done.  Successful.
Performing step fix_calls [dependencies=unknown] ...Done.  Successful.
Performing step move_globals [dependencies=unknown] ...Done.  Successful.
Performing step zax [dependencies=none] ...Done.  Successful.
Performing step zipr [dependencies=none] ...Done.  Successful.
Zafl: success. Output file is: ./tmp/ls.zafl 

```

#### Step 2: Running an instrumented binary
All ZAFL'd binaries can be run normally just as their original uninstrumented versions, e.g.: ```./tmp/ls.zafl```. 

To ensure the binary has been instrumented properly, run: ```ZAFL_DEBUG=1 ./tmp/ls.zafl```. 
The output should start with:
```
Error getting shm environment variable - fake allocate AFL trace map
Success at mmap!
libautozafl: auto-initialize fork server
```

#### Step 3: Fuzzing an instrumented binary
Let's prep a seed directory for fuzzing:
```bash
mkdir in_dir && echo "hello" > in_dir/seed
```
Let's now run the ZAFL'd binary with AFL:
```bash
afl-fuzz -i in_dir -o out_dir -- ./tmp/ls.zafl @@
```
If AFL complains about `Looks like the target binary is not instrumented!`, you'll need to set the following environment variable before fuzzing:
```bash
export AFL_SKIP_BIN_CHECK=1
```
ZAFL's instrumentation also supports AFL's other utilities, e.g.:
```bash
afl-showmap -o map.out -- ./tmp/ls.zafl
afl-cmin -i out_dir/queue/ -o cmin.out -- ./tmp/ls.zafl @@
```

## Using ZAFL for Windows Binaries
ZAFL offers preliminary instrumentation support for Windows 7 PE32+ binaries, though WinAFL-compatible fuzzing instrumentation is not yet supported at this time. Instrumenting Windows binaries must be performed with a *Linux-based* ZAFL install. **This mode requires IDA Pro and the following modified command**:
```bash
zafl.sh <target_binary> <zafl_output_binary> -F --ida --no-stars
```
We hope to improve our Windows support in the near future.

# TL;DR
Once everything is installed properly, you can prepare a binary for fuzzing with the simple command:
```bash
zafl.sh <target_binary> <zafl_output_binary>
```
To see what transformations ZAFL currently supports, simply run ```zafl.sh --help```.

**We welcome any community contributions, and ideas for improvements and new fuzzing transformations!** To open an issue or merge request, please contact one of the developers (`hiser@virginia.edu`, `an7s@virginia.edu`, `jwd@virginia.edu`, or `snagy2@vt.edu`).  **Happy fuzzing!**
