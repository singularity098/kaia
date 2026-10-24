/* Force-included in every translation unit of the portable Linux build.
   Keeps the binaries loadable on older distributions (glibc 2.35: Ubuntu 22.04, Debian 12, Mint 21)
   even when they are compiled on a newer system. See glibc_compat.c for the other half. */
#pragma once
#if defined(__linux__)
#include <features.h>
#if defined(__GLIBC__) && __GLIBC__ == 2 && __GLIBC_MINOR__ >= 38
 /* glibc 2.38 renames strtol/sscanf & co. to __isoc23_* in C2X/GNU mode: use the classic ones */
 #undef __GLIBC_USE_C2X_STRTOL
 #define __GLIBC_USE_C2X_STRTOL 0
 #undef __GLIBC_USE_C23_STRTOL
 #define __GLIBC_USE_C23_STRTOL 0
#endif
#endif
