#ifndef TUNIX_BOOT_BOOT_CORE_H
#define TUNIX_BOOT_BOOT_CORE_H

#include "fw/fw.h"

/* The loader proper, once a backend has a working firmware interface to hand
   it. Everything firmware-specific happened before this was called. */
void boot_core(const struct fw_ops *fw);

#endif
