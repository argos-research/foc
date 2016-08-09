INTERFACE [sched_edf]:

#include "ready_queue_edf.h"
#include <cxx/dlist>
#include "member_offs.h"
#include "types.h"
#include "globals.h"

class Sched_context : public cxx::D_list_item //public Sched_context_edf<Sched_context>
{
	MEMBER_OFFSET();
	friend class Jdb_list_timeouts;
	friend class Jdb_thread_list;
	friend class Ready_queue_edf<Sched_context>;

	template<typename T>
	friend class Jdb_thread_list_policy;

	 union Sp
	 {
	    L4_sched_param p;
	    L4_sched_param_deadline deadline;
	 };


public:
	typedef Sched_context Edf_sc;

	 typedef cxx::Sd_list<Sched_context> Edf_list;

	 class Ready_queue_base : public Ready_queue_edf<Sched_context>
	  {
	  public:
		Ready_queue_edf<Sched_context> edf_rq;

		Sched_context *current_sched() const { return _current_sched; }
	    void activate(Sched_context *s)
	    { _current_sched = s; }

/*
	    void ready_enqueue(Sched_context *sc)
	    {
	      assert_kdb(cpu_lock.test());

	      // Don't enqueue threads which are already enqueued
	      if (EXPECT_FALSE (sc->in_ready_list()))
	        return;

	      enqueue(sc, (sc == current_sched()));
	    }
*/
	    void enqueue(Sched_context *sc, bool is_current);
//	    {
//	    	 edf_rq.enqueue(sc, is_current);
//	    }
	    void deblock_refill(Sched_context *sc);
	    void requeue(Sched_context *sc);
	    void dequeue(Sched_context *sc);

	  private:
	    Sched_context *_current_sched;
	  };


	 //void deblock_refill(Sched_context *sc);

	 Context *context() const { return context_of(this); }


private:

	static Sched_context *edf_elem(Sched_context *x) { return x; }

	// CRUCIAL: _ready_link must have the same memory location as _ready_next from Fp_sc for in_ready_list()
	Sched_context **_ready_link;
	bool _idle:1;
	unsigned _dl;

	Unsigned64 _left;

	unsigned short _p;
	unsigned _q;

};

IMPLEMENTATION [sched_edf]:

#include <cassert>
#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"
#include "config.h"

#include "kobject_dbg.h"
#include "debug_output.h"

PUBLIC
Sched_context::Sched_context()
{
	dbgprintf("[Sched_context] Created Sched_context object with type:Deadline\n");
	_p = 0;
	_dl = Config::Default_deadline;
	_q = Config::Default_time_slice;
	_left = Config::Default_time_slice;
	_ready_link = 0;
}

PUBLIC inline
unsigned
Sched_context::deadline() const
{
  return _dl;
}

PUBLIC inline
Context *
Sched_context::owner() const
{
  return context();
}

PUBLIC inline
Mword
Sched_context::in_ready_list() const
{
  //return Edf_list::in_list(this);
	return _ready_link != 0;
}


IMPLEMENT inline
void
Sched_context::Ready_queue_base::deblock_refill(Sched_context *sc)
{
    edf_rq.deblock_refill(sc);
}

/**
 * Enqueues the Sched_context object in the corresponding ready-list
 */

IMPLEMENT
void
Sched_context::Ready_queue_base::enqueue(Sched_context *sc, bool is_current)
{
    dbgprintf("[Sched_context::enqueue] Enqueuing Sched_context object in edf_rq (id:%lx, dl:%d)\n",
               Kobject_dbg::obj_to_id(sc->context()),
               sc->deadline());
    edf_rq.enqueue(sc, is_current);
}

/**
 * Removes the Sched_context object from the corresponding ready-list
 */
IMPLEMENT inline
void
Sched_context::Ready_queue_base::dequeue(Sched_context *sc)
{
	dbgprintf("[Sched_context::dequeue] Dequeuing Sched_context object in edf_rq (id:%lx, dl:%d)\n",
			  Kobject_dbg::obj_to_id(sc->context()),
			  sc->deadline());
	edf_rq.dequeue(sc);
}


IMPLEMENT
void
Sched_context::Ready_queue_base::requeue(Sched_context *sc)
{
	edf_rq.requeue(sc);
}


PUBLIC
int
Sched_context::set(L4_sched_param const *_p)
{
	Sp const *p = reinterpret_cast<Sp const *>(_p);

	if (p->deadline.deadline == 0)
		return -L4_err::EInval;

	dbgprintf("[Sched_context::set] Set type to Deadline (id:%lx, dl:%ld)\n",
			 Kobject_dbg::obj_to_id(this->context()),
			 p->deadline.deadline);
	_p = 0;
	_dl = p->deadline.deadline;
	_q = Config::Default_time_slice;



	return 0;
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{
	return deadline() < sc->deadline();
}

PUBLIC inline
void
Sched_context::replenish()
{
  _left = _q;
}
PUBLIC static inline
unsigned
Sched_context::prio()
{
	return 0;
}

PUBLIC inline
void
Sched_context::set_left(Unsigned64 l)
{
  _left = l;
}


/**
 * Return remaining time quantum of Sched_context
 */
PUBLIC inline
Unsigned64
Sched_context::left() const
{
  return _left;
}


