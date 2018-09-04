# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs. 

Key features of Zafl:
* Uses Zipr, a fast, space-efficient binary rewriter to inline AFL-style instrumentations
  * Preliminary overhead: 
     * On average 20% slower than afl/source code
     * 15% faster than afl/dyninst
     * a **lot** faster than afl/QEMU

## Installation
Note that you will need **sudo** privileges to get and install all the required packages.
```bash
git clone --recursive git@git.zephyr-software.com:allnp/zafl_umbrella.git
cd zafl_umbrella
. set_env_vars
./get_packages.sh
./build-all.sh
```

Note:
* Zafl automatically downloads and builds AFL
* Building Zafl takes approximately 10 minutes