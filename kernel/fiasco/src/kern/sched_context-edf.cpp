INTERFACE [sched_edf]:

#include "ready_queue_edf.h"

class Sched_context : public Sched_context_edf<Sched_context>
{
	MEMBER_OFFSET();
	friend class Jdb_list_timeouts;
	friend class Jdb_thread_list;
	friend class Ready_queue_edf<Sched_context>;

	template<typename T>
	friend class Jdb_thread_list_policy;
	//friend class Sched_context_edf<Sched_context>;

	 union Sp
	 {
	    L4_sched_param p;
	    L4_sched_param_deadline deadline;
	 };

public:
	 typedef cxx::Sd_list<Sched_context>  Edf_list;

	 class Ready_queue_base : public Ready_queue_edf<Sched_context>
	  {
	  public:
		Sched_context *current_sched() const { return _current_sched; }
	    void activate(Sched_context *s)
	    { _current_sched = s; }

	    void ready_enqueue(Sched_context *sc)
	    {
	      assert_kdb(cpu_lock.test());

	      // Don't enqueue threads which are already enqueued
	      if (EXPECT_FALSE (sc->in_ready_list()))
	        return;

	      enqueue(sc, sc == current_sched());
	    }

	    void deblock_refill(Sched_context *sc);

	  private:
	    Sched_context *_current_sched;
	  };


	 void deblock_refill(Sched_context *sc);

	 Context *context() const { return context_of(this); }



private:

	 static Sched_context *edf_elem(Sched_context *x) { return x; }


	  Sched_context **_ready_link;
	  bool _idle:1;
	  Unsigned64 _dl;
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
	_dl = metric;
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
  return Edf_list::in_list(this);
}

/*
IMPLEMENT inline
void
Sched_context::deblock_refill(Sched_context *sc)
{
    deblock_refill(sc);
}
*/
PUBLIC
int
Sched_context::set(L4_sched_param const *_p)
{
	Sp const *p = reinterpret_cast<Sp const *>(_p);
	if (p->p.sched_class != L4_sched_param_deadline)
		return -L4_err::EInval;
	{
		if (p->deadline.deadline == 0)
			return -L4_err::EInval;

		dbgprintf("[Sched_context::set] Set type to Deadline (id:%lx, dl:%ld)\n",
				 Kobject_dbg::obj_to_id(this->context()),
				 p->deadline.deadline);
		_p = 0;
		_dl = p->deadline.deadline;
		_q = Config::Default_time_slice;

	}

	return 0;
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{ return deadline() < sc->deadline(); }
