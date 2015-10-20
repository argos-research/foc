#ifndef __ETHREAD_H
#define __ETHREAD_H

#include <l4/haecer/perfmon.h>

#ifdef __cplusplus
        #define EXT_C extern "C"
#else
        #define EXT_C 
#endif

typedef struct {
        long long tsc;
        double power;
} eData;

EXT_C void printEThread();
EXT_C void setupEThread(RAPLCounter rc);
EXT_C void getEData(eData** buffer);


#endif //__ETHREAD_H
