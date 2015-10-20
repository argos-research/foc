/* (C) 2011 Marcus Haehnel
 * 
 * Note: This implementation is currently specific to intel processors
 *       It will NOT work on AMD processors and will detect wether it is
 *       running on the correct processor.
 *
 *	 Feel free to extend it to support amd64 architectures
 */
#include "haecer.h"
#include <sys/time.h>
#include <math.h>
#include <float.h>
//DEBUG:
#include <unistd.h>

#define WITH_DELAY 1

//#define NDEBUG //if you do not want to use assertions in this file!
#include <assert.h>


//Strings for the Architectural counters. Printed on disabled info
const char* ev_types[] = { "core cycle", "instruction retired", "reference cycles",
                "last level cache reference", "last level cache misses",
		"branch instruction retired", "branch mispredict retired"};

//Used to match the model number (index) to the architecture for 
//counter availability
unsigned int procType[MAX_ARCH+1] = {
	UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, 
	UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF,
        UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, 
	UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, UNDEF,
        UNDEF, UNDEF, UNDEF, UNDEF, UNDEF, WESTMERE, UNDEF, UNDEF,
	UNDEF, UNDEF, SANDY_BRIDGE, UNDEF, WESTMERE, UNDEF, UNDEF};

perfmon info;		//the variable holding architecture information
int failed = 0;		//starting failed because of unsupported architecture
int has_ferret = 0;	//indicates if ferret support is availbale (cap)
l4_cap_idx_t ferret;	//the capability used to communicate with ferret
uint32_t* usedPerfCounters;	//ptr to array indicating used counters
double ePerTSC = 0;
PerfCounter invalidCounter = newCounter( 0, Invalid, 0) ; //Invalid Counter 


//Convenience macro used to return from functions on failed perfmon startup
#define fail_check(val) if (failed) { printf("Performance monitoring not available!\n"); return val; }
	
//Convenience macros to handle the counter usage array
#define setPerfCounterUsed(id) usedPerfCounters[(id)>>5] |= 1<<(id&0x1F)
#define isPerfCounterUsed(id) (usedPerfCounters[(id)>>5] & (1<<(id&0x1F)))
#define resetPerfCounterUsed(id) usedPerfCounters[(id)>>5] &= ~(1<<(id&0x1F))

//Convenience macro to check if the selected architectural counter is available
//on the current machine using the global info structure
#define hasArchCounter(counter) !((1<<(counter&0xF))&(info.enummask))

			
void find_counters() {
	l4_umword_t a, b, d;
	
	asm volatile("CPUID" : "=a" (a), "=b" (b), "=d" (d) : "a" (0x0A) : "ecx");
	//printf("CPUID.0Ah: EAX: %08x  EBX: %08x ECX: %08x\n",a,b,d);	
	info.arch_version = a&0xFF;
	a >>= 8; info.counters = a&0xFF;
	a >>= 8; info.bitwidth = a&0xFF;
	a >>= 8; info.bits4enum = a&0xFF;

	if (info.arch_version > 1) {
		info.ff_counters = d&(0x1F);
		d >>= 5; info.ff_bitwidth = d&(0xFF);
	} else {
		info.ff_counters = 0; 
		info.ff_bitwidth = 0;
	}
	info.enummask = b&((2<<(info.bits4enum+1))-1);
	usedPerfCounters = (uint32_t*)malloc(info.counters/32+1);
	for (unsigned int i = 0; i <= info.counters/32; i++)  usedPerfCounters[i] = 0;
}

// this returns a performance counter that monitors the desired event. 
// if no free counters were available or the event type is not supported on the
// current platform then the invalid counter is returned.
//
// BEWARE! If multiple programs use this library at the same time they will
// disturb each other and overwrite each others sensors. The correct solution
// to this situation is to make the library availbale in a perfmon server
// which multiplexes between individual applications and manages the sensor
PerfCounter setupNonArchCounter(event ev, umask um, int flags, char mask) {
	assert(0 && "Setting up non architectural counters might be buggy");
	fail_check(invalidCounter);
	if ( !(ev & 0xFFFFFF & procType[info.cpu_model]) | !(um & 0xFFFFFF & procType[info.cpu_model])) {
		printf("Warning! Event 0x%02x Umask 0x%02x not available on CPU!\n",ev>>24,um>>24);
		return invalidCounter;
	}
	PerfCounter id = newCounter( 0, Performance, 0 );
	for (id.perfID = 0; id.perfID < info.counters; id.perfID++) {
		if (!isPerfCounterUsed(id.perfID)) break;
	}
	if (id.perfID == info.counters) return invalidCounter;
	setPerfCounterUsed(id.perfID);
	id.perfRegister = 0xC1+id.perfID;
	disableCounter(&id);
	wrmsr(id.perfRegister,0); //Reset counter
	wrmsr(0x186+id.perfID,ev>>24 | ((um>>24)<<8) | flags | (mask << 24));
	return id;
}

PerfCounter setupArchCounter(ARCH_counter ctr, int flags, char mask) {
	assert(0 && "Setting up architectural counters might be buggy");
	fail_check(invalidCounter);
	if (!hasArchCounter(ctr)) {
		printf("Warning! Architectural counter 0x%02x not available on this CPU!\n",ctr&0xF);
		return invalidCounter;
	}
	return setupNonArchCounter((ctr<<20&0xFF000000)|0xFFFFFF,(ctr<<12&0xFF000000)|0xFFFFFF,flags,mask);
}

PerfCounter setupFFCounter(FF_counter ctr, int flags) {
	assert(0 && "Setting up fixed-function counters might be buggy");
	fail_check(invalidCounter);

	if ((info.arch_version < 2) | ((ctr>>4) > 0x308+info.ff_counters)) {
		printf("Warning! Fixed function counter 0x%02x not available on this CPU!\n",ctr&0xF);
		return invalidCounter;
	}
	
	PerfCounter id = newCounter (ctr>>4, FixedFunctionPerformance, ctr&0xF);

	unsigned long long value =rdmsr(0x38D); //PERF_FNC_CTRL
	wrmsr( 0x38D, (value & ~(0xF<<(id.perfID*4))) | (flags<<(id.perfID*4)));
//	wrmsr(flags, d, 0x38D); TODO
//	rdmsr(0x38F,a,d); //PERF_GLOBAL_CTRL
//	wrmsr(a, (d | (1<<id)), 0x38F); 
	return id;
}

PerfCounter setupEnergyCounter(RAPLCounter type) {
	//the timestamp of the read should be in there so we can tell the time for energy
	int reg = 0;
	switch (type) {
		case RAPL_PKG: reg = 0x611; break;
		case RAPL_PP0: reg = 0x639; break;
		case RAPL_PP1: reg = 0x641; break;
		default: return invalidCounter;
	}
	PerfCounter id = newCounter( reg, Energy, 0 );
	unsigned long long value =rdmsr(0x606);	//Read unit ...
	id.perfID = (value>>8)&0x1F;		//and store the x from *1/2^x in perfID
	return id;
}

void disableCounter(PerfCounter *ctr) {
	assert(0 && "Disabling counters might be buggy");
	fail_check();
	
	//FF counters must be disabled using the setupFFCounter with flags = 0, todo: incorporate
	if (ctr->counterType != Performance) return;
	unsigned long long value = rdmsr(0x186+ctr->perfID);
	value &= (-1 ^ PERF_ENABLE);
	wrmsr(0x186+ctr->perfID,value);
}

void enableCounter(PerfCounter *ctr) {
	assert(0 && "Enabling counters might be buggy");
	fail_check();
	unsigned long long value = rdmsr(0x38F);
	if (ctr->counterType == FixedFunctionPerformance) {
		wrmsr(0x38F,value|(1<<ctr->perfID));
	} else if (ctr->counterType == Performance) {
		wrmsr(0x38F,value|(1<<ctr->perfID));
	}
	if (ctr->counterType != Performance) return;
	value = rdmsr(0x186+ctr->perfID);
	value |= (PERF_ENABLE);
	wrmsr(0x186+ctr->perfID,value);
}

void resetCounter(PerfCounter *ctr) {
	assert(0 && "Resetting counters might be buggy");
	if (ctr->counterType != Performance) return;
	wrmsr(ctr->perfRegister,0);
}

void freeCounter(PerfCounter *ctr) {
	assert(0 && "Freeing counters might be buggy");
	fail_check();
	if (ctr->counterType != Performance) return;
	disableCounter(ctr);
	resetCounter(ctr);
	//Free the reserverd ferret memory! TODO
	resetPerfCounterUsed(ctr->perfID);
}

void simultaneousReadCounter(PerfCounter* ctr, unsigned long long * buffer, int count) {
	assert(0 && "Simultaenous counter reading might be buggy");
	//FixMe unimplemented ATM!
	//rdmsr(0x38F,a,d)
//	wrmsr(0,0,0x38F);	//Disable all at once
	for (int i = 0; i < count; i++) {
		buffer[i] = readCounter(&ctr[i]);
	}
//	wrmsr((1<<(info.ff_counters+1))-1,(1<<(info.counters+1))-1,0x38F); //This might not be ok. Better to
				//enable only availbale counters TODO!
	//wrmsr(a,d,0x38F)
}


double minE = DBL_MAX;
double maxE = 0;

unsigned int startValue;
unsigned long long startT;
precInfo lastRead;

__attribute__ ((noinline)) unsigned long long waitForUpdate() {
	unsigned long long old = 0, cur = rdmsr(0x611);
	unsigned register long long endTSC;
	do {
		old = cur;
		endTSC = l4_rdtsc();
		cur = rdmsr(0x611);
	} while (old == cur);
	return endTSC;
}

void startPowerRead(RAPLCounter val) {
	startEnergyRead(val);
	startT = l4_rdtsc();
}

double endPowerRead(RAPLCounter val) {
	unsigned long long endT = l4_rdtsc();
	double E = endEnergyRead(val);
	return (E/((endT-startT)/2500000000.0)); //TODO! This limits us to My box!
}

void startEnergyRead(RAPLCounter val) {
/*	switch (val) {
		case RAPL_PKG: reg = 0x611; break;
		case RAPL_PP0: reg = 0x639; break;
		case RAPL_PP1: reg = 0x641; break;
		default: reg = 0x611;
	}*/
//only do pkg for now
       // unsigned long long startTSC,endTSC;
	//startTSC = l4_rdtsc();
/*        do {
		endTSC = startTSC;	
	        startTSC = l4_rdtsc();
        } while(startTSC-endTSC < 100000);*/
	unsigned int curE,lastE;
	curE = rdmsr(0x611);
#if WITH_DELAY
	do {
		lastE = curE;
		curE = rdmsr(0x611);
	} while (curE == lastE);
	startValue = curE;
#endif
}

double endEnergyRead(RAPLCounter val) {
	unsigned register long long startTSC = l4_rdtsc();
	unsigned long long endTSC;
/*	switch (val) {
		case RAPL_PKG: reg = 0x611; break;
		case RAPL_PP0: reg = 0x639; break;
		case RAPL_PP1: reg = 0x641; break;
		default: reg = 0x611;
	}*/
	//only do pkg for now
/*	rdmsr(reg,a,d);
	curE = a;
	do {
		lastE = curE;
		rdmsr(reg,a,d);
		endTSC = rdtscp();
		curE = a;
	} while (curE == lastE);*/
//	rdmsr(0x611,startE,d);
#if WITH_DELAY
	endTSC = waitForUpdate();
#endif
	unsigned long long value = rdmsr(0x611);
/*	lastRead.cyclesIntroduced = (endTSC-startTSC);
	lastRead.energyRead = (curE-startValue)*getEnergyUnit();
	lastRead.introducedEnergy = (endTSC-startTSC)*ePerTSC;
	lastRead.minIntroducedEnergy = (endTSC-startTSC)*minE;
	lastRead.maxIntroducedEnergy = (endTSC-startTSC)*maxE;
*/	
	return ((value-startValue)*getEnergyUnit()-(endTSC-startTSC)*ePerTSC);
}

/*precInfo getPrecissionInfo() {
	return lastRead;
}*/

double getEnergyUnit() {
	static unsigned long long unit = 0;

	if (unit == 0)
		unit = (rdmsr(0x606)>>8)&0x1F;

	return 1/pow(2,unit);
}

unsigned long long readCounter(PerfCounter *ctr) {
	fail_check(-1);
	struct timeval t;
	gettimeofday(&t, NULL);
	unsigned long long result;

	if (ctr->counterType == Invalid) return -1;

	unsigned long long value = rdmsr(ctr->perfRegister);
	if (ctr->counterType == Energy) {
		result = value;
		result |= (value&(1<<31))?0xFFFFFFFF00000000:0; 
	} else {
		assert(0 && "Reading normal counters is buggy!");
		result = value;
		result<<=32;
		result+=value;
	}
#ifdef USE_FERRET
	if (ctr->list != NULL) {
		int idx = ferret_list_dequeue(ctr->list);
 	        ferret_list_entry_common_t* elc = 
			(ferret_list_entry_common_t *)ferret_list_e4i(ctr->list->glob, idx);
		elc->major     = ctr->ferret_major;
	        elc->minor     = ctr->ferret_minor;
	        elc->instance  = 0;
	        elc->cpu       = 0;	//This should be fixed  for multi cpu!
	        elc->data64[0] = result;  // can be up to six. Note this for sim. Read
		elc->data64[1] = ctr->perfID;
		elc->data64[2] =  t.tv_sec * 1000000 + t.tv_usec;

	        ferret_list_commit(ctr->list, idx);
	}
#endif
	return result;
}


int setupFerretMonitor(PerfCounter *ctr, uint16_t major, uint16_t minor) {
#ifdef USE_FERRET
	if (!has_ferret) return -1;
	ctr->ferret_major = major;
	ctr->ferret_minor = minor;
	return ferret_create(ferret,major,minor,0,FERRET_LIST,
			FERRET_SUPERPAGES,"64:3000",ctr->list,&malloc);
#else	
	(void) ctr; (void) major; (void) minor;
	return -1;
#endif
}

double getEPerTSC(RAPLCounter ctr);
double getEPerTSC(RAPLCounter ctr) {
	unsigned register long long startTSC,endTSC, curE, startE, lastE, totalE;
	double register result = 0;
/*	switch(ctr) {
		case RAPL_PKG: reg = 0x611; break;
		case RAPL_PP0: reg = 0x639; break;
		case RAPL_PP1: reg = 0x641; break;
		default: reg = 0x611;
	}*/
	//wait for smm ...
	printf("Calibrating ...\n");
//only PKG for now
	unsigned long long value;
	for (int i = 0; i < 1000; i++) {
		startTSC = l4_rdtsc();
/*		do {
			endTSC=startTSC;
			startTSC = rdtscp();
		} while(startTSC-endTSC < 100000);
		//after this we saw a SMM and have silence for 16ms (more or less)*/
		value = rdmsr(0x611); //RAPL_PKF
		startE = value;
		do {
			lastE = startE;
			startTSC = l4_rdtsc();
			value = rdmsr(0x611);
			startE = value;
		} while(lastE == startE);//wait for first counter update
		endTSC = waitForUpdate();
		curE = rdmsr(0x611);
		double ePerT = ((curE-startE)*getEnergyUnit()/(endTSC-startTSC));
		totalE = curE-startE;
		if (ePerT > maxE) { maxE = ePerT; printf("AVG: %f, NewMax: %lli | %f\n",((result+ePerT)/(i+1))*1000000,(endTSC-startTSC),ePerT*1000000); };
		if (ePerT < minE) { minE = ePerT; printf("AVG: %f, NewMin: %lli | %f\n",((result+ePerT)/(i+1))*1000000,(endTSC-startTSC),ePerT*1000000); };
//		printf("Needed %.16f J for %lli TSC ==> %.16f uJ/TSC\n",totalE*getEnergyUnit(),
//			endTSC-startTSC,((totalE*getEnergyUnit()/(endTSC-startTSC))*10000000));
		result += ePerT;
	}
	result /= 1000;
	printf("Average: %.16f uJ/TSC\n",result*1000000);
	printf("Max Deviation: +%f %% / -%f %%\n",
		((result-minE)/result)*100,
		((maxE-result)/result)*100);
#if 1
	printf("Testing ...\n");
	char* temp =(char*)malloc(10240);
	for (int i = 0; i < 10240; i++) {
		temp[i] = '\n';
	}
	curE = rdmsr(0x611); //RAPL_PKG
	do {
		lastE = curE;
		startTSC = l4_rdtsc();
		value = rdmsr(0x611);
		curE = value;
	} while(lastE == curE);//wait for first counter update
	startE=curE;
	endTSC = waitForUpdate();
	curE = rdmsr(0x611);
	printf("Estimated: %f Got: %f Deviation: %f\n",(endTSC-startTSC)*result,(curE-startE)*getEnergyUnit(),((curE-startE)*getEnergyUnit())/((endTSC-startTSC)*result)-1);
#endif
	return result; //minE; //result;
}


//This function is used to initialize the library. It analyzes the architecture
//and determines available sensors. Unless NON_VERBOSE is defined it outputs
//its findings
static void __attribute__ ((constructor)) haecer_init()
{
	l4_umword_t a,b,c,d;

	VERBOSE(printf("Hello from HAECer - The HAEC energy reader!\n"));

	asm volatile("cpuid" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (0x00));
	
	if (b != 0x756e6547 || d != 0x49656e69 || c != 0x6c65746e) {
		printf("Did not detect Intel CPU ... aborting\n");
		failed = 1;
		return;
	}


	if (a < 0x0A) {
		printf("Enumeration with CPUID is not supported by this CPU!\n");
		printf("HAECer does not support your CPU at the moment. Terminating\n");
		failed = 1;
		return;
	} else  {
		VERBOSE(printf("CPUID supported ... enumerating counters\n")); 
		find_counters();
	}
	VERBOSE(	
		printf("Architectural Performance Monitoring Version:   %i\n",info.arch_version);
		printf("           Programmable Counters per Processor: %i\n",info.counters);
		printf("           Bitwidth per programmable Counter:   %i\n",info.bitwidth);
		printf("           fixed-function counters:             %i\n",info.ff_counters);
		printf("           Bitwidth of fixed-function counter:  %i\n",info.ff_bitwidth);
		printf("           Disabled Events:\n");
		for (unsigned int i = 0; i < info.bits4enum; i++) {
			if (!hasArchCounter(i)) printf("              %s event\n",ev_types[i]);	
		}
		//printf("Analyzing SMM ...\n");
	)
	ePerTSC = getEPerTSC(RAPL_PKG);
	
	asm volatile("cpuid" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (0x01));

	info.cpu_family = (a>>8&0xF); 
	info.cpu_model = (a>>4&0xF);
	if (info.cpu_family == 0x0F || info.cpu_family == 0x06) info.cpu_model += (a>>16&0x0F)<<4;
	if (info.cpu_family == 0x0F) info.cpu_family += (a>>20&0xFF);
	if (info.cpu_family != 0x06) {
		printf("Only family 6 CPUs are supported for now!\n");
		failed = 1;
	}
	VERBOSE(printf("Family: %02x Model: %02x (Mask: %03x)\n",info.cpu_family,info.cpu_model,procType[info.cpu_model]));
#ifdef USE_FERRET
	VERBOSE(printf("Trying to connect with ferret ...\n"));
	ferret = l4re_get_env_cap("ferret");
	if (!l4_is_invalid_cap(ferret)) {
		VERBOSE(printf("Connected\n"));
		has_ferret = 1;
	} else {
		printf("Could not find ferret in namespace. Reading to ferret not available\n");
		has_ferret = 0;
	}
#else
	VERBOSE(printf("Built without ferret support!\n"));
	has_ferret = 0;
#endif
	VERBOSE(printf("HAECer setup complete.\n"));

#if 0
	printf("Performing RAPL Benchmark:\n");
	unsigned long long startTSC;
	unsigned int startE,endE,totalE,minE,maxE;
	for (int i = 0; i < 100; i++) {
		totalE = 0;
		minE = -1;
		maxE = 0;
		for (int j = 0; j < 1000; j++) {
			startTSC = waitForUpdate();
			rdmsr(0x611,startE,d);
			usleep(10*(100-i));
			waitForUpdate();
//			while (rdtscp() < startTSC+2600000);
			rdmsr(0x611,endE,d);
			if (endE-startE > maxE) maxE = endE-startE;
			if (endE-startE < minE) minE = endE-startE;
			totalE += (endE-startE);
		}
		printf("%i %% Idle => %f J | Min: %f | Max: %f\n",i,(totalE/1000)*getEnergyUnit(),
			minE*getEnergyUnit(),maxE*getEnergyUnit());
	}

#endif
}
