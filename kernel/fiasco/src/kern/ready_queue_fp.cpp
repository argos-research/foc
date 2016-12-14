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
  bool switch_rq(List *list, unsigned prio) {
  		assert_kdb(cpu_lock.test());

  		prio_next[prio].exchange(list);

  		typename List::BaseIterator it = List::iter(prio_next[prio].front());
  		dbgprintf("After exchange fp_rq: ");
  		do
  		{
  			dbgprintf("%lx => ",Kobject_dbg::obj_to_id(it->context()));
  		}while (++it != List::iter(prio_next[prio].front()));
  		dbgprintf("end\n");

  		return true;
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

