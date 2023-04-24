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

#if USE_QTHREAD
using WorkerListNode = struct QThreadListNode;
#else
using WorkerListNode = struct StdThreadListNode;
#endif

class ScheduleBasic : public Schedule
{
    typedef PollingWorker WorkerTaskType;
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
    void schedule( DAG &dag )
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

        /* where diverge to each scheduler's own start_task() implementation */
        (this)->start_tasks();

        wait_tasks_finish();

        return;
    }


    static bool shouldExit( Task* task )
    {
        if( ! Singleton::allocate()->taskHasInputPorts( task ) &&
            ! Singleton::allocate()->dataInReady( task, null_port_value ) )
        {
            return true;
        }
        return task->stopped;
    }


    static bool readyRun( Task* task )
    {
        return Singleton::allocate()->dataInReady( task, null_port_value );
    }


    static void precompute( Task* task )
    {
        //std::cout << task->id << std::endl;
    }


    static void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        //Singleton::allocate()->taskCommit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            task->stopped = true;
        }
    }

    static void reschedule( Task* task )
    {
        /* NOTE: Every scheduler should shadow this method with their own
         * implementation */
        auto *worker( static_cast< WorkerTaskType* >( task ) );
        ScheduleBasic::worker_yield( worker );
    }

    static void prepare( Task* task )
    {
    }

    static void postexit( Task* task )
    {
        if( ONE_SHOT != task->type )
        {
            Singleton::allocate()->invalidateOutputs( task );
        }
#if USE_UT
        waitgroup_done( task->wg );
#else
        *task->finished = true; /* let main thread know this task is done */
#endif
    }

protected:

    static inline void worker_yield( WorkerTaskType *worker )
    {
        if( 64 <= ++worker->poll_count )
        {
            worker->poll_count = 0;
            raft::yield();
        }
    }

    static inline int get_nclones( Kernel * const kernel )
    {
        return std::max( 1, kernel->getCloneFactor() );
    }

    template< class SCHEDULER, class WORKERTYPE >
    static inline void start_worker( Kernel * const kernel, const int nclones )
    {
        std::size_t worker_id =
            task_id.fetch_add( nclones, std::memory_order_relaxed );
        for( int i( 0 ); nclones > i; ++i )
        {
            auto *worker( new WORKERTYPE() );
            worker->kernel = kernel;
            worker->id = worker_id + i;
            worker->clone_id = i;

            Singleton::allocate()->taskInit( worker );

            /* now run the worker in a thread */
            int64_t gid( worker->clone_id + worker->kernel->getGroup() );
#if USE_UT
            worker->wg = &wg;
            rt::Spawn( [ worker ](){ worker->template exe< SCHEDULER >();
                                     delete worker; },
                       false, gid );
#else
            auto *tnode( new WorkerListNode(
                        ( SCHEDULER* )nullptr, worker, gid ) );
            /* ( SCHEDULER* )nullptr is just to enable parameter deduction */
            insert_task_node( tnode );
#endif
        }

        return;
    }

    /** kernel set **/
    kernelset_t kernels;
    kernelset_t source_kernels;
    kernelset_t sink_kernels;

private:

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
            ntasks += ScheduleBasic::get_nclones( k );
        }
        waitgroup_add( &wg, ntasks );
#endif
        for( auto * const k : container )
        {
            start_worker< ScheduleBasic, WorkerTaskType >(
                    k, ScheduleBasic::get_nclones( k ) );
        }
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
    typedef CondVarWorker WorkerTaskType;
public:
    ScheduleCV() : ScheduleBasic()
    {
    }
    virtual ~ScheduleCV() = default;

    static void prepare( Task *task )
    {
        Singleton::allocate()->registerConsumer( task );
    }

    static void reschedule( Task *task )
    {
        auto *worker( static_cast< WorkerTaskType* >( task ) );
#if ! IGNORE_HINT_0CLONE
        if( 0 == worker->kernel->getCloneFactor() )
        {
#endif
            /* if ignore 0-clone hint, wait is the only and default action */
            worker->wait< ScheduleCV >();
#if ! IGNORE_HINT_0CLONE
        }
        else
        {
            ScheduleCV::worker_yield( worker );
        }
#endif
    }

private:

    /* Note: s/ScheduleBasic/ScheduleCV/g of ScheduleBasic::start_tasks() */
    virtual void start_tasks() override
    {
        auto &container( kernels );
#if USE_UT
        std::size_t ntasks = 0;
        for( auto * const k : container )
        {
            ntasks += ScheduleCV::get_nclones( k );
        }
        waitgroup_add( &wg, ntasks );
#endif
        for( auto * const k : container )
        {
            start_worker< ScheduleCV, WorkerTaskType >(
                    k, ScheduleCV::get_nclones( k ) );
        }
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BASIC_HPP */
