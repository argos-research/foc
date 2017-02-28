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


public:
  void set_idle(E *sc)
  { sc->_prio = Config::Kernel_prio; }

  void enqueue(E *, bool);
  void dequeue(E *);
  E *next_to_run() const;
  bool empty(unsigned prio) {return prio_next[prio].empty();}
  bool switch_rq(int* info) {
	int num_sub = info[0];
	for(int j=0; j<256; j++) {
		int pos=0;
		typename List::BaseIterator it = prio_next[j].begin();
		if(Kobject_dbg::obj_to_id(it->context())<1000) {
		do
		{
			++it;
			pos=pos+1;
		}while (it != prio_next[j].begin());
		E *tmp_contexts[pos];
		for(int i=0; i<pos; i++)
		{
			E *tmp = (*prio_next[j].begin());
			tmp_contexts[i]=tmp;
			dequeue(tmp);
		}
		for(int i=pos-1;i>=0;i--)
		{
			E *tmp = tmp_contexts[i];
			requeue(tmp);
		}
		}
  		
  	}
	return true;
   }
   void _get_rqs(int* info) {
	int elem_counter=1;
	for(int j=0; j<256; j++) {
			int pos=1;
			typename List::BaseIterator it = List::iter(prio_next[j].front());
			if(Kobject_dbg::obj_to_id(it->context())<1000) {
  			//dbgprintf("Prio list: %d\n",j);
  			do
  			{
  				//dbgprintf("ID:%d ",Kobject_dbg::obj_to_id(it->context()));
				info[2*elem_counter-1]=(int)Kobject_dbg::obj_to_id(it->context());
				info[2*elem_counter]=pos;
				//dbgprintf("Pos RQ:%d\n", pos);
				pos++;
				elem_counter++;
				++it;
  			}while (it != List::iter(prio_next[j].front()));
			//dbgprintf("\n");
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

