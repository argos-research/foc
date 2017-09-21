INTERFACE [sched_fp_edf]:

#include "ready_queue_fp.h"
#include "ready_queue_edf.h"

class Sched_context
{
  MEMBER_OFFSET();
  friend class Jdb_list_timeouts;
  friend class Jdb_thread_list;
  friend class Ready_queue_edf<Sched_context>;

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
    L4_sched_param_deadline deadline;
  };

  struct Ready_list_item_concept
  {
    typedef Sched_context Item;
    static Sched_context *&next(Sched_context *e) { return e->_sc.fp._ready_next; }
    static Sched_context *&prev(Sched_context *e) { return e->_sc.fp._ready_prev; }
    static Sched_context const *next(Sched_context const *e)
    { return e->_sc.fp._ready_next; }
    static Sched_context const *prev(Sched_context const *e)
    { return e->_sc.fp._ready_prev; }
  };

public:
  typedef enum Sc_type
  {
    Fixed_prio = 1,
    Deadline   = 2
  }
  Sc_type;

  typedef cxx::Sd_list<Sched_context, Ready_list_item_concept> Fp_list;
  typedef cxx::Sd_list<Sched_context, Ready_list_item_concept> Edf_list;

private:
  Sc_type _t;

  struct B_sc
  {
    unsigned short _p;
    unsigned _q;
    Unsigned64 _left;

    unsigned prio() const { return _p; }
  };

  struct Fp_sc : public B_sc
  {
    Sched_context *_ready_next, *_ready_prev;
  };

  struct Edf_sc : public Sched_context_edf<Edf_sc>, public B_sc
  {
    // CRUCIAL: _ready_link must have the same memory location as _ready_next from Fp_sc for in_ready_list()
    Sched_context **_ready_link;
    bool _idle:1;
    unsigned _dl;
  };

  union Sc
  {
    Edf_sc edf;
    Fp_sc fp;
  };

  Sc _sc;

public:
  static Edf_sc *edf_elem(Sched_context *x) { return &x->_sc.edf; }

  struct Ready_queue_base
  {
  public:
    Ready_queue_fp<Sched_context> fp_rq;
    Ready_queue_edf<Sched_context> edf_rq;
    Sched_context *current_sched() const { return _current_sched; }
    void activate(Sched_context *s)
    {
      if (!s || s->_t == Deadline)
      edf_rq.activate(s);
      _current_sched = s;
    }

    void enqueue(Sched_context *sc, bool is_current);
    void dequeue(Sched_context *);
    void requeue(Sched_context *sc);

    bool empty(unsigned);  //gmc
    bool switch_rq(int* info); //gmc
    void _get_rqs(int* info);
    void _get_dead(long long unsigned* info);
    void _add_dead(int id, long long unsigned time);

    void set_idle(Sched_context *sc)
    { sc->_t = Deadline; sc->_sc.edf._p = 0; edf_rq.set_idle(sc); }

    Sched_context *next_to_run() const;
    void deblock_refill(Sched_context *sc);

  private:
    friend class Jdb_thread_list;
    Sched_context *_current_sched;
  };

  Context *context() const { return context_of(this); }
};


IMPLEMENTATION [sched_fp_edf]:

#include <cassert>
#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"
#include "config.h"

#include "kobject_dbg.h"
#include "debug_output.h"

/**
 * Constructor
 */
PUBLIC
Sched_context::Sched_context()
{
  _t = Fixed_prio;
  _sc.fp._p = Config::Default_prio;
  _sc.fp._q = Config::Default_time_slice;
  _sc.fp._left = Config::Default_time_slice;
  _sc.fp._ready_next = 0;
}

/**
 * Constructor with type and metric as parameters
 */
PUBLIC
Sched_context::Sched_context(Sc_type type, unsigned metric)
{
  if (type == Fixed_prio)
  {
    _t = Fixed_prio;
    _sc.fp._p = metric;
    _sc.fp._q = Config::Default_time_slice;
    _sc.fp._left = Config::Default_time_slice;
    _sc.fp._ready_next = 0;
  }
  else
  {
    _t = Deadline;
    _sc.edf._p = 0;
    _sc.edf._dl = metric;
    _sc.edf._q = Config::Default_time_slice;
    _sc.edf._left = Config::Default_time_slice;
    _sc.edf._ready_link = 0;
  }
}

IMPLEMENT inline
Sched_context *
Sched_context::Ready_queue_base::next_to_run() const
{
  Sched_context *s = fp_rq.next_to_run();
  if (s)
    return s;

  return edf_rq.next_to_run();
}

/**
 * Check if the Sched_context object is enqueued
 * @return 1 if thread is in ready-list, 0 otherwise
 */
PUBLIC inline
Mword
Sched_context::in_ready_list() const
{
  // This magically works for the fp list and the heap,
  // because wfq._ready_link and fp._ready_next are the
  // same memory location
  if(_t==Fixed_prio||_t>=0)
    return Fp_list::in_list(this);
  else
    return _sc.edf._ready_link != 0;
  return false;
}

/**
 * Returns the type of the Sched_context object
 */
PUBLIC inline
Sched_context::Sc_type
Sched_context::type() const
{
  return _t;
}

/**
 * Returns the metric of the Sched_context object used for taking scheduling decisions
 */
PUBLIC inline
unsigned
Sched_context::metric() const
{
  if (_t == Fixed_prio)
    return _sc.fp._p;
  else
    return _sc.edf._dl;
}

/**
 * Returns the priority of the Sched_context object
 */
PUBLIC inline
unsigned
Sched_context::prio() const
{
  return _sc.fp._p;
}

/**
 * Returns the relative deadline of the Sched_context object
 */
PUBLIC inline
unsigned
Sched_context::deadline() const
{
  return _sc.edf._dl;
}

PUBLIC
int
Sched_context::set(L4_sched_param const *_p)
{
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  if (p->p.sched_class >= 0)
  {
    // legacy fixed prio
    _t = Fixed_prio;
    _sc.fp._p = p->legacy_fixed_prio.prio;
    if (p->legacy_fixed_prio.prio > 255)
      _sc.fp._p = 255;

    _sc.fp._q = p->legacy_fixed_prio.quantum;
    if (p->legacy_fixed_prio.quantum == 0)
      _sc.fp._q = Config::Default_time_slice;
    return 0;
  }
  switch (p->p.sched_class)
  {

    case L4_sched_param_deadline::Class:
      if (p->deadline.deadline == 0)
        return -L4_err::EInval;
      _t = Deadline;
      _sc.edf._p = 0;
      _sc.edf._dl = p->deadline.deadline;
      _sc.edf._q = Config::Default_time_slice;

      break;

    case L4_sched_param_fixed_prio::Class:
      _t = Fixed_prio;

      _sc.fp._p = p->fixed_prio.prio;
      if (p->fixed_prio.prio > 255)
        _sc.fp._p = 255;

      if (p->fixed_prio.quantum == 0)
        _sc.fp._q = Config::Default_time_slice;
      else
        _sc.fp._q = p->fixed_prio.quantum;

      break;

    default:
      return L4_err::ERange;
  };

  /*
   * 'rq' is the CPU-specific ready queue object that is linked to our Ready_queue_base implementation.
   * Dequeuing & enqueuing ensures that the Sched_context object is enqueued in the appropriate ready queue
   * since its type may have changed due to this set operation.
   */
  //rq.current().dequeue(this);
  //rq.current().enqueue(this, this == rq.current().current_sched());

  return 0;
}

IMPLEMENT inline
void
Sched_context::Ready_queue_base::deblock_refill(Sched_context *sc)
{
  if (sc->_t != Deadline)
    fp_rq.deblock_refill(sc);
  else
  if (sc->_t == Deadline)
    edf_rq.deblock_refill(sc);
}

/**
 * Enqueues the Sched_context object in the corresponding ready-list
 */
IMPLEMENT
void
Sched_context::Ready_queue_base::enqueue(Sched_context *sc, bool is_current)
{
  if (sc->_t == Fixed_prio)
  {
    fp_rq.enqueue(sc, is_current);
  }
  else
  {
    edf_rq.enqueue(sc, is_current);
  }
}

/**
 * Removes the Sched_context object from the corresponding ready-list
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", "kdb_ke.h", "std_macros.h"]
void
Sched_context::Ready_queue_base::dequeue(Sched_context *sc)
{
  if (sc->_t == Fixed_prio)
    fp_rq.dequeue(sc);
  else
    edf_rq.dequeue(sc);
}

IMPLEMENT
void
Sched_context::Ready_queue_base::requeue(Sched_context *sc)
{
  if (sc->_t == Fixed_prio)
    fp_rq.requeue(sc);
  else
    edf_rq.requeue(sc);
}

IMPLEMENT
bool
Sched_context::Ready_queue_base::empty(unsigned prio) //gmc
{
  //if (sc->_t == Fixed_prio)
    return fp_rq.empty(prio);
//  else
//    edf_rq.requeue(sc);
}

IMPLEMENT
bool
Sched_context::Ready_queue_base::switch_rq(int* info) //gmc
{
	if(info[1]==0)
		return fp_rq.switch_rq(info);
	else
		return edf_rq.switch_rq(info);
}

IMPLEMENT
void
Sched_context::Ready_queue_base::_get_rqs(int* info)
{
	if(info[1]==0)
		fp_rq._get_rqs(info);
	else
		edf_rq._get_rqs(info);
}

IMPLEMENT
void
Sched_context::Ready_queue_base::_get_dead(long long unsigned* info)
{
	fp_rq._get_dead(info);
}

IMPLEMENT
void
Sched_context::Ready_queue_base::_add_dead(int id, long long unsigned time)
{
	fp_rq._add_dead(id,time);
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{
  if (_t == Fixed_prio)
    return prio() > sc->prio();

  if (_sc.edf._idle)
    return false;

  if (sc->_t == Fixed_prio)
    return false;

  return _sc.edf._dl < sc->_sc.edf._dl;
}

PUBLIC inline
void
Sched_context::replenish()
{
  if (_t == Fixed_prio)
    _sc.fp._left = _sc.fp._q;
  else
    _sc.edf._left = _sc.edf._q;  
}

PUBLIC inline
void
Sched_context::set_left(Unsigned64 l)
{
  if (_t == Fixed_prio)
    _sc.fp._left = l;
  else
    _sc.edf._left = l;
}

PUBLIC inline
Unsigned64
Sched_context::left() const
{
  if (_t == Fixed_prio)
    return _sc.fp._left;
  else
    return _sc.edf._left;
}
