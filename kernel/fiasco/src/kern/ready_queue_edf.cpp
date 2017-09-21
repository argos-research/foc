INTERFACE[sched_fp_edf || sched_edf]:

#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include <cxx/dlist>
#include "config.h"
#include "kobject_dbg.h"
#include "debug_output.h"

#define ANSI_BOLD          "\x1b[1m"
#define ANSI_BOLD_RESET    "\x1b[0m"

struct L4_sched_param_deadline : public L4_sched_param
{
  enum : Smword { Class = -3 };
  unsigned deadline;
};

template< typename E >
class Ready_queue_edf
{
  friend class Jdb_thread_list;
  template<typename T>
  friend class Jdb_thread_list_policy;

public:
  E *current_sched() const { return _current_sched; }
  void activate(E *s) { _current_sched = s; }
  E *idle;

  void set_idle(E *sc)
  {
    idle = sc;
    _e(sc)->_ready_link = &idle;
    _e(sc)->_idle = 1;
  }

  void enqueue(E *, bool is_current);
  void dequeue(E *);
  E *next_to_run() const;

  void _add_dead(int id, long long unsigned time) {
  }

  void _get_dead(long long unsigned* info) {
  }

  bool switch_rq(int* info) {
	//dbgprintf("deploy rq edf\n");
	return false;
  }

  void _get_rqs(int* info) {
	//dbgprintf("get rq edf\n");
	int elem_counter=1;
			typename List::BaseIterator it = List::iter(rq.front());
			if(Kobject_dbg::obj_to_id(it->context())<1000) {
  			do
  			{
				info[2*elem_counter-1]=(int)Kobject_dbg::obj_to_id(it->context());
				//info[2*elem_counter]=_e(it)->_dl;
				elem_counter++;
				++it;
  			}while (it != List::iter(rq.front()));
			}
	info[0]=elem_counter-1;
  }

private:
  typedef typename E::Edf_list List;
  List rq;
  unsigned short count;

  E *_current_sched;

  static typename E::Edf_sc *_e(E *e) { return E::edf_elem(e); }
};

template< typename IMPL >
class Sched_context_edf
{
public:
  bool operator <= (Sched_context_edf const &o) const
  { return _impl()._dl <= o._impl()._dl; }

  bool operator < (Sched_context_edf const &o) const
  { return _impl()._dl < o._impl()._dl; }

private:
  IMPL const &_impl() const { return static_cast<IMPL const &>(*this); }
  IMPL &_impl() { return static_cast<IMPL &>(*this); }
};


// --------------------------------------------------------------------------
IMPLEMENTATION [sched_fp_edf || sched_edf]:

#include <cassert>
#include "config.h"
#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"

#include "debug_output.h"

IMPLEMENT inline
template<typename E>
E *
Ready_queue_edf<E>::next_to_run() const
{
  if (count)
    return rq.front();

  if (_current_sched)
    _e(idle)->_dl = _e(_current_sched)->_dl;

  return idle;
}

/**
 * Enqueues context in ready-list
 */
IMPLEMENT
template<typename E>
void
Ready_queue_edf<E>::enqueue(E *i, bool /*is_current_sched*/)
{
  assert_kdb(cpu_lock.test());

  // Don't enqueue threads which are already enqueued
  if (EXPECT_FALSE (i->in_ready_list()))
    return;

  _e(i)->_ready_link = &i;

  // Insert new Sched_context at the right position,
  // e.g. keep ascending order of the queue from short to large deadlines
  //dbgprintf("[Ready_queue_edf::enqueue] Inserted id:%lx", Kobject_dbg::obj_to_id(i->context()));
  typename List::BaseIterator it;
  if (rq.empty())
  {
    rq.push_front(i);
    //dbgprintf(" at front\n");
  }
  else
  {
    unsigned deadline = i->deadline();
    bool inserted = false;
    it = List::iter(rq.front());
    do
    {
      if (deadline < it->deadline())
      {
        //dbgprintf(" (different deadlines: %d vs. %d)\n", deadline, it->deadline());
        rq.insert_before(i, it);
        inserted = true;
        if (it == List::iter(rq.front()))
          rq.rotate_to(i); // Inserted at the front of the list, so setting a new head is necessary
      }
      else if (deadline == it->deadline())
      {
        //dbgprintf(" (same deadline: %d)\n", deadline);
        rq.insert_after(i, it);
        inserted = true;
      }
    }
    while (!inserted && ++it != List::iter(rq.front()));
    if (!inserted)
    {
      // Has not been enqueued yet
      // Deadline is bigger than any other -> insert at back
      rq.push_back(i);
      //dbgprintf(" at back (deadline: %d)\n", deadline);
    }
  }
  count++;
  // Print content of ready queue
  it = List::iter(rq.front());
  //dbgprintf(ANSI_BOLD "edf_rq: ");
  do
  {
    //dbgprintf("%lx(dl:%d) => ",
    //           Kobject_dbg::obj_to_id(it->context()),
    //           it->deadline());
  }
  while (++it != List::iter(rq.front()));
  //dbgprintf("end\n" ANSI_BOLD_RESET);
}

/**
 * Removes context from ready-list
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", "kdb_ke.h", "std_macros.h"]
template<typename E>
void
Ready_queue_edf<E>::dequeue(E *i)
{
  assert_kdb (cpu_lock.test());

  // Don't dequeue threads which aren't enqueued
  if (EXPECT_FALSE (!i->in_ready_list() || i == idle))
    return;

  rq.remove(i);
  _e(i)->_ready_link = 0;

  if (count==0)
	  return;
  count--;
  typename List::BaseIterator it;
  if (count)
  {
     it = List::iter(rq.front());
     //dbgprintf(ANSI_BOLD "edf_rq: ");
     do
     {
       //dbgprintf("%lx(dl:%d) => ",
       //           Kobject_dbg::obj_to_id(it->context()),
       //           it->deadline());
     }
     while (++it != List::iter(rq.front()));
     //dbgprintf("end\n" ANSI_BOLD_RESET);
  }
  else
  {
    //dbgprintf(ANSI_BOLD "edf_rq: empty\n" ANSI_BOLD_RESET);
  }
}

/**
 * Requeues context to ready-list
 */
PUBLIC
template<typename E>
void
Ready_queue_edf<E>::requeue(E *i)
{
  if (!i->in_ready_list())
  {
    //dbgprintf("Got requeue call for id:%lx\n", Kobject_dbg::obj_to_id(i->context()));
    enqueue(i, false);
  }
}


PUBLIC template< typename E > inline
void
Ready_queue_edf<E>::deblock_refill(E *sc)
{
  //dbgprintf("Got deblock_refill call for id:%lx\n", Kobject_dbg::obj_to_id(sc->context()));
  _e(sc)->_left = _e(sc)->_q;
}
