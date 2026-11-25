#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>
#include <stdio.h>
#include <default_sched.h>

static struct sched_class *sched_class;
static struct run_queue __rq;
static struct run_queue *rq = &__rq;

static inline void
sched_class_enqueue(struct proc_struct *proc) {
    if (proc != idleproc) {
        assert(proc->state == PROC_RUNNABLE && proc->rq == NULL);
        sched_class->enqueue(rq, proc);
    }
}

static inline void
sched_class_dequeue(struct proc_struct *proc) {
    assert(proc->rq == rq);
    sched_class->dequeue(rq, proc);
}

static inline struct proc_struct *
sched_class_pick_next(void) {
    return sched_class->pick_next(rq);
}

void
sched_class_proc_tick(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->proc_tick(rq, proc);
    } else {
        proc->need_resched = 1;
    }
}

void
sched_init(void) {
    sched_class = &default_sched_class;
    rq->max_time_slice = MAX_TIME_SLICE;
    sched_class->init(rq);
    cprintf("sched class: %s\n", sched_class->name);
}

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
            if (proc != current) {
                sched_class_enqueue(proc);
            }
        } else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

//LAB4 function3 2413042
void schedule(void)
{
    bool intr_flag;
    struct proc_struct *next = NULL;

    // The whole selection plus the switch must be atomic w.r.t. interrupts.
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;

        // The running process is outside the queue until it yields the CPU.
        if (current->state == PROC_RUNNABLE) {
            sched_class_enqueue(current);
        }
        next = sched_class_pick_next();
        if (next != NULL) {
            assert(next->state == PROC_RUNNABLE);
            sched_class_dequeue(next);
        }

        // No runnable thread: fall back to idleproc.
        if (next == NULL)
        {
            next = idleproc;
        }

        next->runs++;
        if (next != current)
        {
            proc_run(next);
        }
    }
    local_intr_restore(intr_flag);
}


