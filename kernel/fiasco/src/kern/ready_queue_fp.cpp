INTERFACE [sched_fixed_prio || sched_fp_wfq || sched_fp_edf]:

#include "config.h"
#include <cxx/dlist>
#include "member_offs.h"
#include "types.h"
#include "globals.h"

#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"

#include "kobject_dbg.h"
#include "debug_output.h"

struct L4_sched_param_fixed_prio : public L4_sched_param
{
  enum : Smword { Class = -1 };
  Mword quantum;
  unsigned short prio;
};

template<typename E>
class Ready_queue_fp
{
  friend class Jdb_thread_list;
  template<typename T>
  friend struct Jdb_thread_list_policy;

private:
  typedef typename E::Fp_list List;
  unsigned prio_highest;
  List prio_next[256];
  long long unsigned dead_threads[100];
  long long unsigned num_dead=0;


public:
  void set_idle(E *sc)
  { sc->_prio = Config::Kernel_prio; }

  void _add_dead(int id, long long unsigned time) {
	if(num_dead==50) 
	{
		num_dead=0;
	}
	dead_threads[2*num_dead]=id;
	dead_threads[2*num_dead+1]=time;
	num_dead++;
  }
  void _get_dead(long long unsigned* info) {
	info[0]=num_dead;
	for(int i=1; i<=num_dead; i++)
	{
		info[2*i-1]=dead_threads[2*i-2];
		info[2*i]=dead_threads[2*i-1];
	}
  }
  void enqueue(E *, bool);
  void dequeue(E *);
  E *next_to_run() const;
  bool empty(unsigned prio) {return prio_next[prio].empty();}
  bool switch_rq(int* info) {
	dbgprintf("deploy rq fp\n");
	int num_sub = info[0];
	for(int i=1; i<=num_sub; i++)
	{
		printf("Thread to be scheduled: %d %d\n",info[2*i],info[2*i+1]);
	}
	for(int j=0; j<256; j++) {
		//printf("Deploying...");
		int pos=0;
		typename List::BaseIterator it = prio_next[j].begin();
		//how many objects are in the queue with prio j?
		if(Kobject_dbg::obj_to_id(it->context())<10000) {
		do
		{
			//printf("count %d\n", Kobject_dbg::obj_to_id(it->context()));
			++it;
			pos=pos+1;
		}while (it != prio_next[j].begin());
		//temporary array for contexts
		E *tmp_contexts[pos];
		//store all elements of the queue in temporary array
		for(int i=0; i<pos; i++)
		{
			E *tmp = (*prio_next[j].begin());
			//printf("dequeue %d\n", Kobject_dbg::obj_to_id(tmp));
			tmp_contexts[i]=tmp;
			dequeue(tmp);
			//Keep important tasks alive
			if(Kobject_dbg::obj_to_id(tmp)<600)
			{
				//printf("requeue %d\n", Kobject_dbg::obj_to_id(tmp));
				requeue(tmp);
			}

		}
		//array for order as supposed in info
		E *ordered_contexts[pos];
		int ordered=0;
		//reorder elements
		for(int i=1; i<=num_sub; i++)
		{
			for(int k=0; k<pos; k++)
			{
				if(Kobject_dbg::obj_to_id(tmp_contexts[k]->context())==info[2*i]&&j==info[2*i+1])
				{
					ordered_contexts[ordered]=tmp_contexts[k];
					ordered++;
				}
			}
		}
		//requeue elements in the order which was supposed in info
		for(int i=0;i<ordered;i++)
		{
			//printf("enqueue %d\n", Kobject_dbg::obj_to_id(ordered_contexts[i]));
			requeue(ordered_contexts[i]);
		}
		}
  		
  	}
	return true;
   }
   void _get_rqs(int* info) {
	dbgprintf("get rq fp\n");
	int elem_counter=1;
	for(int j=0; j<256; j++) {
			typename List::BaseIterator it = List::iter(prio_next[j].front());
			if(Kobject_dbg::obj_to_id(it->context())<1000) {
  			do
  			{
				info[2*elem_counter-1]=(int)Kobject_dbg::obj_to_id(it->context());
				info[2*elem_counter]=j;
				elem_counter++;
				++it;
  			}while (it != List::iter(prio_next[j].front()));
			}
		}
	info[0]=elem_counter-1;
   }
};


// ---------------------------------------------------------------------------
IMPLEMENTATION [sched_fixed_prio || sched_fp_wfq || sched_fp_edf]:

#include <cassert>
#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"
#include "config.h"

#include "kobject_dbg.h"
#include "debug_output.h"

IMPLEMENT inline
template<typename E>
E *
Ready_queue_fp<E>::next_to_run() const
{ return prio_next[prio_highest].front(); }

/**
 * Enqueue context in ready-list.
 */
IMPLEMENT
template<typename E>
void
Ready_queue_fp<E>::enqueue(E *i, bool is_current_sched)
{
  assert_kdb(cpu_lock.test());

  // Don't enqueue threads which are already enqueued
  if (EXPECT_FALSE (i->in_ready_list()))
    return;

  unsigned short prio = i->prio();

  if (prio > prio_highest)
    prio_highest = prio;

  prio_next[prio].push(i, is_current_sched ? List::Front : List::Back);

}

/**
 * Remove context from ready-list.
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", "kdb_ke.h", "std_macros.h"]
template<typename E>
void
Ready_queue_fp<E>::dequeue(E *i)
{
  assert_kdb (cpu_lock.test());

  // Don't dequeue threads which aren't enqueued
  if (EXPECT_FALSE (!i->in_ready_list()))
    return;

  unsigned short prio = i->prio();

  prio_next[prio].remove(i);

  while (prio_next[prio_highest].empty() && prio_highest)
    prio_highest--;
}


PUBLIC inline
template<typename E>
void
Ready_queue_fp<E>::requeue(E *i)
{
  if (!i->in_ready_list())
    enqueue(i, false);
  else
    prio_next[i->prio()].rotate_to(*++List::iter(i));
}


PUBLIC template<typename E> inline
void
Ready_queue_fp<E>::deblock_refill(E *)
{}

