#include <l4/sys/utcb.h>
#include <l4/re/env>
#include <l4/sys/scheduler>
#include <l4/sys/thread.h>
#include <l4/re/util/cap_alloc>
#include <l4/libdebug/libdebug.h>

extern "C" unsigned long long monitor_mwait(volatile void* addr, l4_umword_t expected, int enable_int, int cstate) {
	l4_utcb_mr()->mr[1] = 0xBABE;
	l4_utcb_mr()->mr[2] = 0x0002;   //Monitor MWait call
	l4_utcb_mr()->mr[3] = (l4_umword_t)(addr);
	l4_utcb_mr()->mr[4] = expected;
	if (cstate == 0) cstate = 0xF;
	else cstate --;
	l4_utcb_mr()->mr[5] = (cstate&0xF)<<1 | (enable_int==1); //C state
	l4_thread_stats_time(l4re_global_env->main_thread);
	return ((unsigned long long)l4_utcb_mr()->mr[1] << 32)|l4_utcb_mr()->mr[2];
}

extern "C" void monitor_mwait_info(mmwait *data) {
	l4_utcb_mr()->mr[1] = 0xBABE;
	l4_utcb_mr()->mr[2] = 0x0001;   //status call
	l4_thread_stats_time(l4re_global_env->main_thread);
	data->supported = l4_utcb_mr()->mr[1];
	data->range_min = l4_utcb_mr()->mr[2];
	data->range_max = l4_utcb_mr()->mr[3];
}

extern "C" unsigned long long rdmsr(unsigned int msr) {
	l4_utcb_mr()->mr[1] = 0xBABE;
	l4_utcb_mr()->mr[2] = 0x0003; //rdmsr(0x611)
	l4_utcb_mr()->mr[3] = msr;
	l4_thread_stats_time(l4re_global_env->main_thread);
	return ((unsigned long long)l4_utcb_mr()->mr[1] << 32)|l4_utcb_mr()->mr[2];
}

extern "C" void wrmsr(unsigned int msr, unsigned long long value) {
	l4_utcb_mr()->mr[1] = 0xBABE;
	l4_utcb_mr()->mr[2] = 0x0004;
	l4_utcb_mr()->mr[3] = msr;
	l4_utcb_mr()->mr[4] = value & ((1LL<<32)-1);
	l4_utcb_mr()->mr[5] = (value >> 32);
}
