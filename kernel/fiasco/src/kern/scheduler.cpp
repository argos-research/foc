INTERFACE:

#include "context.h"
#include "icu_helper.h"
#include "types.h"

class Scheduler : public Icu_h<Scheduler>, public Irq_chip_soft
{
  FIASCO_DECLARE_KOBJ();
  typedef Icu_h<Scheduler> Icu;

public:
	enum Operation
	{
		Info       = 0,
		Run_thread = 1,
		Idle_time  = 2,
		Deploy_thread = 3,
		Get_rqs = 4,
	};


	typedef Sched_context::Fp_list List; //gmc

	List list; //gmc

	static Scheduler scheduler;
private:
	Irq_base *_irq;
};

// ----------------------------------------------------------------------------
IMPLEMENTATION:
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "thread_object.h"
#include "l4_buf_iter.h"
#include "l4_types.h"
#include "entry_frame.h"

#include "debug_output.h"


#include "cpu_lock.h"
#include "kdb_ke.h"
#include "std_macros.h"

FIASCO_DEFINE_KOBJ(Scheduler);

Scheduler Scheduler::scheduler;

PUBLIC void
Scheduler::operator delete (void *)
{
  printf("WARNING: tried to delete kernel scheduler object.\n"
         "         The system is now useless\n");
}

PUBLIC inline
Scheduler::Scheduler() : _irq(0)
{
  initial_kobjects.register_obj(this, 7);
}


PRIVATE
L4_msg_tag
Scheduler::sys_run(L4_fpage::Rights, Syscall_frame *f, Utcb const *iutcb, Utcb *outcb)
{
  L4_msg_tag const tag = f->tag();
  Cpu_number const curr_cpu = current_cpu();

  Obj_space *s = current()->space();
  assert(s);

  L4_snd_item_iter snd_items(iutcb, tag.words());

  if (EXPECT_FALSE(tag.words() < 5))
    return commit_result(-L4_err::EInval);

  unsigned long sz = sizeof (L4_sched_param_legacy);

    {
      L4_sched_param const *sched_param = reinterpret_cast<L4_sched_param const*>(&iutcb->values[1]);
      if (sched_param->sched_class < 0)
        sz = sched_param->length;

      sz += sizeof(Mword) - 1;
      sz /= sizeof(Mword);

      if (sz + 1 > tag.words())
	return commit_result(-L4_err::EInval);
    }

  if (EXPECT_FALSE(!tag.items() || !snd_items.next()))
    return commit_result(-L4_err::EInval);

  L4_fpage _thread(snd_items.get()->d);
  if (EXPECT_FALSE(!_thread.is_objpage()))
    return commit_result(-L4_err::EInval);

  Thread *thread = Kobject::dcast<Thread_object*>(s->lookup_local(_thread.obj_index()));
  if (!thread)
    return commit_result(-L4_err::EInval);

  Mword _store[sz];
  memcpy(_store, &iutcb->values[1], sz * sizeof(Mword));
     L4_sched_param const *sched_param = reinterpret_cast<L4_sched_param const *>(_store);

  Thread::Migration info;

  Cpu_number const t_cpu = thread->home_cpu();

  if (Cpu::online(t_cpu) && sched_param->cpus.contains(t_cpu))
    info.cpu = t_cpu;
  else if (sched_param->cpus.contains(curr_cpu))
    info.cpu = curr_cpu;
  else
    info.cpu = sched_param->cpus.first(Cpu::present_mask(), Config::max_num_cpus());

  /* Own work */
#ifdef CONFIG_SCHED_FP_EDF
  if (iutcb->values[5] > 0)
  {
    // Some deadline has been passed, so our thread becomes a deadline-based thread
    dbgprintf("[Scheduler::sys_run] Deadline passed for id:%lx is: %lu\n", thread->dbg_id(), iutcb->values[5]);
    L4_sched_param_deadline sched_p;
    sched_p.sched_class     = -3;
    sched_p.deadline        = iutcb->values[5];
    thread->sched_context()->set(static_cast<L4_sched_param*>(&sched_p));
  }
  else
  {
    info.sp = sched_param;
  }
#else
  info.sp = sched_param;
#endif

  if (0)
    printf("CPU[%u]: run(thread=%lx, cpu=%u (%lx,%u,%u)\n",
           cxx::int_value<Cpu_number>(curr_cpu), thread->dbg_id(),
           cxx::int_value<Cpu_number>(info.cpu),
           iutcb->values[2],
           cxx::int_value<Cpu_number>(sched_param->cpus.offset()),
           cxx::int_value<Order>(sched_param->cpus.granularity()));

  thread->migrate(&info);

  outcb->values[0] = thread->dbg_id();
  outcb->values[1] = Timer::system_clock();

  return commit_result(0,2);
}
PRIVATE
L4_msg_tag
Scheduler::sys_deploy_thread(L4_fpage::Rights, Syscall_frame *f, Utcb const *utcb) //gmc
{
	L4_msg_tag const tag = f->tag();

	Sched_context::Ready_queue &rq = Sched_context::rq.current();

	int list[(int)tag.words()];
	list[0]=((int)tag.words()-1)/2;

	for(int i = 1 ; i < tag.words(); i++){
		list[i]=utcb->values[i];					 
	}

	rq.switch_ready_queue(&list[0]);

	return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_idle_time(L4_fpage::Rights,
                         Syscall_frame *f, Utcb *utcb)
{
  if (f->tag().words() < 3)
    return commit_result(-L4_err::EInval);

  L4_cpu_set cpus = access_once(reinterpret_cast<L4_cpu_set const *>(&utcb->values[1]));
  Cpu_number const cpu = cpus.first(Cpu::online_mask(), Config::max_num_cpus());
  if (EXPECT_FALSE(cpu == Config::max_num_cpus()))
    return commit_result(-L4_err::EInval);

  reinterpret_cast<Utcb::Time_val *>(utcb->values)->t
    = Context::kernel_context(cpu)->consumed_time();

  return commit_result(0, Utcb::Time_val::Words);
}

PRIVATE
L4_msg_tag
Scheduler::sys_info(L4_fpage::Rights, Syscall_frame *f,
                    Utcb const *iutcb, Utcb *outcb)
{
  if (f->tag().words() < 2)
    return commit_result(-L4_err::EInval);

  L4_cpu_set_descr const s = access_once(reinterpret_cast<L4_cpu_set_descr const*>(&iutcb->values[1]));
  Mword rm = 0;
  Cpu_number max = Config::max_num_cpus();
  Order granularity = s.granularity();
  Cpu_number const offset = s.offset();

  if (offset >= max)
    return commit_result(-L4_err::EInval);

  if (max > offset + Cpu_number(MWORD_BITS) << granularity)
    max = offset + Cpu_number(MWORD_BITS) << granularity;

  for (Cpu_number i = Cpu_number::first(); i < max - offset; ++i)
    if (Cpu::present_mask().get(i + offset))
      rm |= (1 << cxx::int_value<Cpu_number>(i >> granularity));

  outcb->values[0] = rm;
  outcb->values[1] = Config::Max_num_cpus;
  return commit_result(0, 2);
}

PRIVATE
L4_msg_tag
Scheduler::sys_get_rqs(L4_fpage::Rights, Syscall_frame *f, Utcb *outcb)
{
	int info[101];
	Sched_context::Ready_queue &rq = Sched_context::rq.current();
	rq.get_rqs(info);
	int num_subjects=info[0];
	outcb->values[0]=num_subjects;
	//printf("Num subjects:%d\n",num_subjects);
	for(int i=1; i<=2*num_subjects; i++)
	{
		//printf("%d\n",info[i]);
		outcb->values[i]=info[i];
	}
	return commit_result(0, info[0]+1);
}

PUBLIC inline
Irq_base *
Scheduler::icu_get_irq(unsigned irqnum)
{
  if (irqnum > 0)
    return 0;

  return _irq;
}

PUBLIC inline
void
Scheduler::icu_get_info(Mword *features, Mword *num_irqs, Mword *num_msis)
{
  *features = 0; // supported features (only normal irqs)
  *num_irqs = 1;
  *num_msis = 0;
}

PUBLIC
L4_msg_tag
Scheduler::icu_bind_irq(Irq *irq_o, unsigned irqnum)
{
  if (irqnum > 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->unbind();

  Irq_chip_soft::bind(irq_o, irqnum);
  _irq = irq_o;
  return commit_result(0);
}

PUBLIC
L4_msg_tag
Scheduler::icu_set_mode(Mword pin, Irq_chip::Mode)
{
  if (pin != 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->switch_mode(true);
  return commit_result(0);
}

PUBLIC inline
void
Scheduler::trigger_hotplug_event()
{
  if (_irq)
    _irq->hit(0);
}

PUBLIC
L4_msg_tag
Scheduler::kinvoke(L4_obj_ref ref, L4_fpage::Rights rights, Syscall_frame *f,
                   Utcb const *iutcb, Utcb *outcb)
{
  switch (f->tag().proto())
    {
    case L4_msg_tag::Label_irq:
      return Icu::icu_invoke(ref, rights, f, iutcb,outcb);
    case L4_msg_tag::Label_scheduler:
      break;
    default:
      return commit_result(-L4_err::EBadproto);
    }

	switch (iutcb->values[0])
	{
	case Info:       return sys_info(rights, f, iutcb, outcb);
	case Run_thread: return sys_run(rights, f, iutcb, outcb);
	case Idle_time:  return sys_idle_time(rights, f, outcb);
	case Deploy_thread: return sys_deploy_thread(rights, f, iutcb);
	case Get_rqs: sys_get_rqs(rights, f, outcb);
	default:         return commit_result(-L4_err::ENosys);
	}
}
