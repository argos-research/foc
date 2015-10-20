#ifndef __PERFMON_H
#define __PERFMON_H

//#ifdef USE_FERRET
	#include <l4/ferret/sensors/list_producer.h>
//#else
//	#include <stdint.h>
//#endif

#ifdef __cplusplus
	#define EXT_C extern "C"
#else
	#define EXT_C 
#endif

//Just because it looks nicer
//Format: <Number of PMC MSR>:<config ID:4>
typedef enum {
	INST_RETIRED_ANY       = 0x3090,	
	CPU_CLK_UNHALTED_CORE  = 0x30A1,
	CPU_CLK_UNHALTED_REF   = 0x30B2
} FF_counter;

//All the architectural counters possibly available
//Format <umask:8>:<eventid:8>:<bitmaskid:4>
//The bitmask id is used to check for bit x in the
//CPUID.0AH EBX register. It is 1 if the counter is unavailable
typedef enum {
	UNHALTED_CORE_CYCLES = 0x003C0,
	INSTRUCTIONS_RETIRED = 0x00C01,
	UNHALTED_REF_CYCLES  = 0x013C2,
	LL_CACHE_REFERENCE   = 0x4F2E3,
	LL_CACHE_MISSES	     = 0x412E4,
	BRANCH_INS_RETIRED   = 0x00C45,
	BRANCH_MIS_RETIRED   = 0x00C56
} ARCH_counter;

typedef unsigned int event;
typedef unsigned int umask;

typedef enum {
        FixedFunctionPerformance, Performance, Energy, Invalid
} cType;

#define isPerfCounterValid(ctr) (ctr.counterType != Invalid)

typedef enum {
	RAPL_PKG, RAPL_PP0, RAPL_PP1
} RAPLCounter;

//The counter type used in nearly all functions that
//work with the counters
typedef struct {
        unsigned int perfRegister;	//register to read/write
        cType counterType;
        unsigned int perfID;	//number of sensor (if applicable)
	unsigned short ferret_major;
	unsigned short ferret_minor;
	ferret_list_local_t* list;
} PerfCounter;
		
// this returns a performance counter that monitors the desired event. 
// if no free counters were available or the event type is not supported on the
// current platform then the invalid counter is returned.
// 
// BEWARE! If multiple programs use this library at the same time they will
// disturbe each other and overwrite each others sensors. The correct solution
// to this situation is to make the library availbale in a perfmon server
// which multiplexes between individual applications and manages the sensor
//
// For architecutral counters
EXT_C PerfCounter setupArchCounter(ARCH_counter ctr, int flags, char mask);
// For fixed function counters
EXT_C PerfCounter setupFFCounter(FF_counter ctr, int flags);
// For non architectural counters
EXT_C PerfCounter setupNonArchCounter(event ev, umask um, int flags, char mask);
// For Energy Counter
EXT_C PerfCounter setupEnergyCounter(RAPLCounter type);


//Interacting with the counters
EXT_C void disableCounter(PerfCounter *ctr);	//Stops counting (does not reset to 0!)
EXT_C void enableCounter(PerfCounter *ctr);	//Starts counting (does not reset to 0!)
EXT_C void resetCounter(PerfCounter *ctr); //Sets counter value to 0
EXT_C unsigned long long int readCounter(PerfCounter *ctr);	//Reads current counter value
//The following makes the counter available again. Reading the same counter
//again after calling this yields undefined results. The counter is reset 
//before it is being freed. 
EXT_C void freeCounter(PerfCounter *ctr); 
EXT_C double getEnergyUnit(void);
//When you want to read the value of more than one counter at the same time, 
//then the following method must be used. It takes an array of PerfCounters and
//writes the results in the same order to the buffer provided by the 
//application. The number of performance counters must be given by passing the
//count variable.
//
//It is the responsibility of the application to provide a large enough buffer for
//all results to fit into it.
//
//Note, that this also writes to possibly attached ferret sensors, but that of
//course the TSC value of the sensors differ. If you want to better correlate the
//sensors the should all be recorded in a common sensor (see next function)
EXT_C void simultaneousReadCounter(PerfCounter* ctr, unsigned long long* buffer, int count);

//TO BE IMPLEMENTED! This attaches a sensor to the simultaneous read. Beware,
//that it must provide enough room for all sensors!
//
//int setupSimultaneousFerretMonitor(uint16_t major, uint16_t minor, int count);

//This sets up a ferret sensor that is then connected to the performance counter
//Whenever this counter is now read, it does not only return the result (which
//may be discarded by the application) but also write this result to the 
//sensor that is selected here.
//This function returns 0, unless ferret is not available
EXT_C int setupFerretMonitor(PerfCounter *ctr, uint16_t major, uint16_t minor);

//Those are used to get more precise measurements with a higher resolution.
//They depend on the fact, that the measured thread is the only thread running.
//The measurement data is interpolated by substracting known idle time till the next
//Counter update.
//The measurement is started precisely after a counter update.
//On the end idle time is inserted till the next update and the energy consumed 
//during this idle time (on average) is subtracted.
EXT_C void startEnergyRead(RAPLCounter);
EXT_C double endEnergyRead(RAPLCounter);
EXT_C void startPowerRead(RAPLCounter);
EXT_C double endPowerRead(RAPLCounter);

extern const char* ev_types[];

typedef struct {
	unsigned int cyclesIntroduced;
	double energyRead;
	double introducedEnergy;
	double minIntroducedEnergy;
	double maxIntroducedEnergy;
} precInfo;

precInfo getPrecissionInfo(void);

//Only(!) Intel family 6 is supported at the moment
#define UNDEF 		0
#define SANDY_BRIDGE 	1<<0
#define	WESTMERE	1<<1
/*#define	PENRYN 		1<<2
#define	MEROM 		1<<3
#define	NEHALEM 	1<<4
#define PENTIUM_PRO	1<<5
#define KLAMATH		1<<6
#define DESCHUTES	1<<7
#define PETNIUM_II	KLAMATH||DESCHUTES
#define MENDOCINO	1<<8
#define KATMAI		1<<9
#define COPPERMINE	1<<10
#define TUALATIN	1<<11
#define PETNIUM_3	KATMAI||COPPERMINE||TUALATIN
#define P3_XEON		1<<12
#define BANIAS		1<<13
#define DOTHAN		1<<14
#define PENTIUM_M	BANIAS||DOTHAN
#define YONAH		1<<15
#define INTEL_CORE	YONAH*/
#define ANY_CPU		0xFFFF
//More to come?

#define MAX_ARCH 0x2E

extern unsigned int procType[MAX_ARCH+1];
//TODO: Done till 24h (inclusive) for westmere

#define Event(event,processorType) ((event&0xFF)<<24)+((processorType)&0xFFFFFF)
#define Umask(umask,processorType) ((umask&0xFF)<<24)+((processorType)&0xFFFFFF)

#define PerfSystem class PEvents
#define perfClass(name) public: class name
#define perfEvent(name,id,architecture) public: class name { public: static const event Event = Event(id,architecture); class masks {
#define perfUmask(name,id,architecture) public: static const umask name = Umask(id,architecture);
#define AliasMask(name,as) public: static const umask name = as;
#define END_EVENT };}; 

/*C implementation (does not work)
#define PerfSystem struct PEvents
#define perfClass(name) struct name
#define perfEvent(name,id,architecture) struct name { static const event Event = Event(id,architecture); struct masks {
#define perfUmask(name,id,architecture) static const umask name = Umask(id,architecture);
#define AliasMask(name,as) static const umask name = as;
#define END_EVENT };};
*/

#ifdef __cplusplus
	#include <l4/haecer/counters_cpp.h>
#else
	#include <l4/haecer/counters_c.h>
#endif

#undef PerfSystem
#undef perfClass
#undef perfEvent
#undef perfUmask
#undef AliasMask
#undef END_EVENT

//(non-)Arch flags
//For use with setup(Non)ArchCounter
#define PERF_USR  	1<<16
#define PERF_OS   	1<<17
#define PERF_EDGE 	1<<18
#define PERF_PC   	1<<19
#define PERF_INT  	1<<20
#define PERF_ENABLE	1<<22
#define PERF_INVERT	1<<23

//FF flags -> for use with setupFFCounter
#define FF_PMI		8
#define FF_OS		1
#define FF_USR		2
#define FF_ANY_THREAD   4

#endif
