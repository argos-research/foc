#include <l4/haecer/eThread.h>
#include <l4/libdebug/libdebug.h>
#include <l4/sys/utcb.h>
#include <l4/re/env.h>
#include <l4/util/util.h>
#include <l4/sys/thread.h>
#include <sys/time.h>
#include <iostream>
#include <stdlib.h>


#define SAMPLE_BUFFER 150000	//150 seconds max

#define BUFFER_SIZE 6000	//6 seconds

//ticks per us
#define CLOCKRATE 2500.0 

using namespace std;

int cur_reg = 0;

unsigned long long  dev_max=0, avg = 0, start_tsc;
eData* samples;
unsigned int sample_ptr;
bool terminateThread = true;

void die(const char* str);
void die(const char* str) {
	cout << "Fatal: " << str << endl;
	exit(-1);
}

pthread_t eThreadInfo;


void* eThread(void* arg);
void* eThread(void* arg) {
	
	unsigned int sample_ptr = 0;
	unsigned long long last, current, first;
	first = last = current = rdmsr((unsigned int)arg);
	double unit = getEnergyUnit();

	while (first == last) {
		last = rdmsr((unsigned int)arg);
	}
	
	unsigned long long last_tsc, cur_tsc;
	last_tsc = cur_tsc = start_tsc = l4_rdtsc();
	while (!terminateThread) {	
		usleep(500);
		cur_tsc = l4_rdtsc();
		current = rdmsr((unsigned int)arg);
		if (cur_tsc - last_tsc < (avg+2*dev_max)) {
//			cout << "TOO SHORT! " << cur_tsc-last_tsc << " vs. " << (avg+dev_max) << endl;
			continue; 
		} //too short ... wait on. Might have been an update ... or might not
		if ((cur_tsc - start_tsc)%avg < dev_max || avg-(cur_tsc-start_tsc)%avg < dev_max) {
//			cout << "TOO CLOSE! " << (cur_tsc-last_tsc)%avg << " vs. " << dev_max << endl;
			continue; //too close to an update ... try the next
		}

		//we are sure we saw at least one update
		unsigned int ucount= (cur_tsc - last_tsc)/(avg); //we saw this many to be exact
		unsigned int tcount = (cur_tsc - start_tsc)/(avg);
		if (ucount < 1) {
			cout << "ERRORRRRR!" << endl;
		}
		if (ucount >= 1 && current == last) {
			cout << "DEV ERROR!" << " Start: " << start_tsc << " Cur: " << cur_tsc << " Last: " << last_tsc << " AVG: " << avg << endl;
		}
		//Warning! This does not handle overflows!
		samples[sample_ptr].power = (unit*(current-last))/((double)(avg*ucount)/(CLOCKRATE*1000000));
		samples[sample_ptr++].tsc = cur_tsc;
		last_tsc = start_tsc + avg*tcount; //The last update point
		last = current;         //The energy at this point
	}
	return (void*)sample_ptr;
}

void printEThread() {
	void* elements;
	terminateThread = true;
	pthread_join(eThreadInfo,&elements);
	cout << "Got " << (unsigned int)elements << " samples" << endl;
	cout << "Start TSC: " << start_tsc << endl;
	cout << "TSC;Power" << endl;
	for (unsigned int i = 0; i < (unsigned int)elements; i++) {
		cout << samples[i].tsc-start_tsc << ";" << samples[i].power <<  endl;
	}
	cout << "Complete!" << endl;
}

void setupEThread(RAPLCounter rc) {
	unsigned long long* buffer = (unsigned long long*)malloc(sizeof(unsigned long long)*BUFFER_SIZE);
	samples = (eData*)malloc(sizeof(eData)*SAMPLE_BUFFER);
	
	if (!buffer) die("Unable to initialize buffer!");
	cout << "Initializing synchronisation ..." << endl;
	
	switch (rc) {
		case RAPL_PKG: cur_reg = 0x611; break;
		default: die("Can not set up unknown RAPL coounter"); //TODO! Implement PKG0 / PKG1
	}
	
	unsigned long long curE,lastE = rdmsr(cur_reg);
	
	for (int ctr = 0; ctr < BUFFER_SIZE; ctr++) {
		do {
			curE = rdmsr(cur_reg);
		} while (curE == lastE);
		buffer[ctr] = l4_rdtsc();
		lastE = curE;
	}
	avg = dev_max = 0;

	//Evaluate results ...
	for (int i = BUFFER_SIZE-1; i > 0; i--) {
		buffer[i] -= buffer[i-1];
		avg += buffer[i];
	}
	avg /= BUFFER_SIZE-1;
	
	for (int i = 1; i< BUFFER_SIZE; i++) {
		if (avg != buffer[i] && abs(buffer[i]-avg) > dev_max) dev_max = abs(buffer[i]-avg);
	}

	cout << "Start TSC: " << buffer[0] << endl;
	cout << "Average cycles per update: " << avg << endl;
	cout << "Max dev from average: +/-" << dev_max << endl;
	//Starting up ...
	terminateThread = false;
	pthread_attr_t* attr = static_cast<pthread_attr_t*>(malloc(sizeof(pthread_attr_t)));
	pthread_attr_init(attr);
	struct sched_param prio = { 10 };
	pthread_attr_setschedparam(attr,&prio);
	if (pthread_create(&eThreadInfo,attr, eThread,(void*)cur_reg))  {
		cout << "Failed to create thread!" << endl;
	} else {
		cout << "Measurement thread created ... running" << endl;
	}
}

