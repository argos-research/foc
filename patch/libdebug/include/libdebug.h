#ifndef __LIBDEBUG_H
#define __LIBDEBUG_H

typedef struct mmwait_t {
	unsigned int supported;
	unsigned int range_min;
	unsigned int range_max;
} mmwait;
#ifdef __cplusplus
	#define EXT_C extern "C"
#else
	#define EXT_C 
#endif

EXT_C unsigned long long rdmsr(unsigned int msr);
EXT_C void wrmsr(unsigned int msr, unsigned long long value);
EXT_C void monitor_mwait_info(mmwait *data);

//returns processor specific wakeup tsc
EXT_C unsigned long long monitor_mwait(volatile void* addr, l4_umword_t expected, int enable_int, int cstate);


#endif //LIBDEBUG_H
