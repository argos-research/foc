#include <l4/sys/utcb.h>
#include <l4/util/util.h>
#include <stdio.h>
#include <stdlib.h>
#include <l4/sys/thread.h>
#include <l4/re/env.h>
#include <l4/haecer/perfmon.h>
#include <l4/util/util.h>
#include <l4/libdebug/libdebug.h>

#define newCounter(reg, type, id) {reg, type, id, -1, -1, NULL}

#ifdef USE_FERRET
	#include <l4/ferret/client.h>
#endif

#define rdpmc(reg,a,d) __asm__ __volatile__ ("rdpmc":"=a" (a), "=d" (d) : "c" (reg));

//Wrappers for verbose output
//#define NON_VERBOSE

#ifndef NON_VERBOSE
#define VERBOSE(ins) ins
#else
#define VERBOSE(ins) while (false) { ins; }
#endif

//internal stucture to record features of monitoring architecture
typedef struct {
	unsigned int arch_version;
        unsigned int counters;
        unsigned int bitwidth;
        unsigned int bits4enum;
        unsigned int enummask;
        unsigned int ff_counters;
        unsigned int ff_bitwidth;
        unsigned int cpu_model;
        unsigned int cpu_family;
} perfmon;

//interal functions (not for external usage!)
void find_counters();

