#ifndef _LIBZAFL_HPP_
#define _LIBZAFL_HPP_

extern "C" {
// config.h is a header from the afl distro
// make sure afl has been downloaded and AFL_PATH is set properly

extern void zafl_initAflForkServer();
extern void zafl_bbInstrument(unsigned short id);

}

#endif
