/**
 * schedule_basic.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Mon Feb 27 17:24:00 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2014 Jonathan Beard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef RAFT_SCHEDULE_SCHEDULE_BASIC_HPP
#define RAFT_SCHEDULE_SCHEDULE_BASIC_HPP  1
#include <affinity>

#if QTHREAD_FOUND
#include <qthread/qthread.hpp>
#endif
#if UT_FOUND
#include <ut>
#endif

#include "raftinc/signalhandler.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/kernelkeeper.tcc"
#include "raftinc/sysschedutil.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/task.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/pollingworker.hpp"

namespace raft {

struct StdThreadListNode : public TaskListNode
{
    StdThreadListNode( Task *the_task ) :
        TaskListNode( the_task ),
        th( [ & ](){ (this)->task->finished = &finished;
                     (this)->task->exe(); } )
    {
    }

    virtual ~StdThreadListNode()
    {
        th.join();
        delete task;
    }

    std::thread th;
};

#if QTHREAD_FOUND
struct QThreadListNode : public TaskListNode
{
    QThreadListNode( Task *the_task ) : TaskListNode( the_task )
    {
        (this)->task->finished = &finished;
        qthread_spawn( QThreadListNode::run,
                       ( void* ) task,
                       0,
                       0,
                       0,
                       nullptr,
                       task->kernel->getGroup() % qthread_num_shepherds(),
                       0 );
    }

    virtual ~QThreadListNode()
    {
        delete task;
    }

    static aligned_t run( void *data )
    {
        auto * const worker( reinterpret_cast< Task* >( data ) );
        worker->exe();
        return 0;
    }
};
#endif

#if USE_QTHREAD
using PollingWorkerListNode = struct QThreadListNode;
#else
using PollingWorkerListNode = struct StdThreadListNode;
#endif


class ScheduleBasic : public Schedule
{
public:
    ScheduleBasic() : Schedule()
    {
    }

    virtual ~ScheduleBasic() = default;

    /**
     * schedule - called to start execution of all
     * kernels.  Implementation specific so it
     * is purely virtual.
     */
    virtual void schedule( DAG &dag )
    {
        kernels = dag.getKernels();
        source_kernels = dag.getSourceKernels();
        sink_kernels = dag.getSinkKernels();
#if USE_UT
        runtime_start( static_wrapper, this );
#else
        doSchedule();
#endif
    }

    void doSchedule()
    {

        while( ! Singleton::allocate()->isReady() )
        {
            raft::yield();
        }

        (this)->start_tasks();

        wait_tasks_finish();

        return;
    }


    virtual bool shouldExit( Task* task )
    {
        if( ! Singleton::allocate()->taskHasInputPorts( task ) &&
            ! Singleton::allocate()->getDataIn( task, null_port_value ) )
        {
            return true;
        }
        return task->stopped;
    }


    virtual bool readyRun( Task* task )
    {
        return Singleton::allocate()->getDataIn( task, null_port_value );
    }


    virtual void precompute( Task* task )
    {
        //std::cout << task->id << std::endl;
    }


    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        Singleton::allocate()->commit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            task->stopped = true;
        }
    }

    virtual void reschedule( Task* task )
    {
        auto *worker( static_cast< PollingWorker* >( task ) );
        if( 64 <= ++worker->poll_count )
        {
            worker->poll_count = 0;
            raft::yield();
        }
    }

    virtual void prepare( Task* task )
    {
    }

    virtual void postexit( Task* task )
    {
        Singleton::allocate()->invalidateOutputs( task );
#if USE_UT
        waitgroup_done( task->wg );
        delete task; /* main thread is only monitoring wg, so self-free */
#else
        *task->finished = true; /* let main thread know this task is done */
#endif
    }

protected:

#if USE_UT
    static void static_wrapper( void *arg )
    {
        auto *sched( static_cast< ScheduleBasic* >( arg ) );
        sched->doSchedule();
    }
#endif

    virtual void start_tasks()
    {
        auto &container( kernels );
#if USE_UT
        std::size_t ntasks = 0;
        for( auto * const k : container )
        {
            ntasks += k->getCloneFactor();
        }
        waitgroup_add( &wg, ntasks );
#endif
        for( auto * const k : container )
        {
            (this)->start_polling_worker( k );
        }
    }

    virtual void start_polling_worker( Kernel * const kernel )
    {
        const int nclones =
            ( kernel->getCloneFactor() > 1 ) ? kernel->getCloneFactor() : 1;

        std::size_t worker_id = task_id.fetch_add(
                nclones, std::memory_order_relaxed );
        for( int i( 0 ); nclones > i; ++i )
        {
            /**
             * thread function takes a reference back to the scheduler
             * accessible done boolean flag, essentially when the
             * kernel is done, it can be rescheduled...and this
             * handles that.
             */
            PollingWorker *task ( new_a_worker() );
            task->kernel = kernel;
            task->id = worker_id + i;
            task->clone_id = i;

            //assert( Singleton::allocate()->isReady() );
            Singleton::allocate()->taskInit( task );

            run_worker( task );
        }

        return;
    }

    /**
     * new_a_worker - simply get the pointer to a new PollingWorker instance
     * made this a virtual methid in protected section in order to allow
     * ScheduleCV to substitue it with a PollingWorkerCV instance
     * @return PollingWorker*
     */
    virtual PollingWorker *new_a_worker()
    {
        return new PollingWorker();
    }

    /** kernel set **/
    kernelset_t kernels;
    kernelset_t source_kernels;
    kernelset_t sink_kernels;

private:

    inline void run_worker( Task *task )
    {
#if USE_UT
        task->wg = &wg;
        rt::Spawn( [ task ](){ task->exe(); } );
#else
        auto *tnode( new PollingWorkerListNode( task ) );
        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        /* insert into tasks linked list */
        tnode->next = tasks->next;
        tasks->next = tnode;
        /** we got here, unlock **/
        tasks_mutex.unlock();
#endif
    }

};

/**
 * ScheduleCV - a special scheduler that is almost identical to the basic
 * polling worker style scheduler (i.e., ScheduleBasic) in most part, except
 * this one exploit the producer-consumer relationship in DAG topology to
 * accurately schedule the tasks getting data ready to make progress.
 * Note: should be used together with AllocateCV.
 */
class ScheduleCV : public ScheduleBasic
{
public:
    ScheduleCV() : ScheduleBasic()
    {
    }
    virtual ~ScheduleCV() = default;

    virtual void prepare( Task *task )
    {
        Singleton::allocate()->registerConsumer( task );
    }

    virtual void reschedule( Task *task )
    {
        auto *worker( static_cast< PollingWorkerCV* >( task ) );
        worker->wait();
    }

protected:

    virtual PollingWorker *new_a_worker()
    {
        return new PollingWorkerCV();
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BASIC_HPP */
