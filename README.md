# Zafl (Zipr-based AFL)

Welcome to *Zafl*, a project to fuzz X86 64-bit binary programs.

## Installation
Note that you will need sudo privileges to get and install all the required packages.
```bash
git clone --recursive git@git.zephyr-software.com:allnp/zafl_umbrella.git
cd zafl_umbrella
. set_env_vars
./get_packages.sh
./build-all.sh
```

Building Zafl takes approximately XX minutes.