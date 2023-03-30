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

struct StdThreadSchedMeta : public TaskSchedMeta
{
    StdThreadSchedMeta( Task *the_task ) :
        TaskSchedMeta( the_task ),
        th( [ & ](){ (this)->task->sched_meta = this;
                     (this)->task->exe(); } )
    {
        run_count = 0;
    }

    virtual ~StdThreadSchedMeta()
    {
        th.join();
        auto *worker( static_cast< PollingWorker* >( task ) );
        delete worker;
    }

    std::thread th;
    /* map every task to a kthread */
    int8_t run_count;
};

#if QTHREAD_FOUND
struct QThreadSchedMeta : public TaskSchedMeta
{
    QThreadSchedMeta( Task *the_task ) : TaskSchedMeta( the_task )
    {
        run_count = 0;
        task->sched_meta = this;
        qthread_spawn( QThreadSchedMeta::run,
                       ( void* ) this,
                       0,
                       0,
                       0,
                       nullptr,
                       task->kernel->getGroup() % qthread_num_shepherds(),
                       0 );
    }

    virtual ~QThreadSchedMeta()
    {
        auto *worker( static_cast< PollingWorker* >( task ) );
        delete worker;
    }

    static aligned_t run( void *data )
    {
        auto * const tmeta( reinterpret_cast< QThreadSchedMeta* >( data ) );
        tmeta->task->exe();
        return 0;
    }

    int8_t run_count;
};
#endif

#if UT_FOUND
struct UTSchedMeta : public TaskSchedMeta
{
    UTSchedMeta( Task *the_task, waitgroup_t *the_wg ) :
        TaskSchedMeta( the_task ), wg( the_wg )
    {
        run_count = 0;
        task->sched_meta = this;
        rt::Spawn( [ & ](){ task->exe(); } );
    }

    virtual ~UTSchedMeta()
    {
        auto *worker( static_cast< PollingWorker* >( task ) );
        delete worker;
    }

    virtual void done()
    {
        waitgroup_done( wg );
    }

    int8_t run_count;
    waitgroup_t *wg;
};
#endif

#if USE_UT
using PollingWorkerSchedMeta = struct UTSchedMeta;
#elif USE_QTHREAD
using PollingWorkerSchedMeta = struct QThreadSchedMeta;
#else
using PollingWorkerSchedMeta = struct StdThreadSchedMeta;
#endif


class ScheduleBasic : public Schedule
{
public:
    ScheduleBasic() : Schedule()
    {
#if USE_UT
        waitgroup_init( &wg );
#elif USE_QTHREAD
        const auto ret_val( qthread_initialize() );
        if( 0 != ret_val )
        {
            std::cerr << "failure to initialize qthreads runtime, exiting\n";
            exit( EXIT_FAILURE );
        }
#endif
    }
    virtual ~ScheduleBasic()
    {
#if USE_QTHREAD
        /** kill off the qthread structures **/
        qthread_finalize();
#endif
    }

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

#if USE_UT
        waitgroup_wait( &wg );
#else
        bool keep_going( true );
        while( keep_going )
        {
            while( ! tasks_mutex.try_lock() )
            {
                raft::yield();
            }
            //exit, we have a lock
            keep_going = false;
            TaskSchedMeta *tparent( tasks );
            //loop over each thread and check if done
            while( nullptr != tparent->next )
            {
                if( tparent->next->finished )
                {
                    TaskSchedMeta *ttmp = tparent->next;
                    tparent->next = ttmp->next;
                    delete ttmp;
                }
                else /* a task ! finished */
                {
                    tparent = tparent->next;
                    keep_going = true;
                }
            }
            //if we're here we have a lock and need to unlock
            tasks_mutex.unlock();
            /**
             * NOTE: added to keep from having to unlock these so frequently
             * might need to make the interval adjustable dep. on app
             */
            std::chrono::milliseconds dura( 3 );
            std::this_thread::sleep_for( dura );
        }
#endif
        return;
    }


    virtual bool shouldExit( Task* task )
    {
        if( ! Singleton::allocate()->taskHasInputPorts( task ) &&
            ! Singleton::allocate()->getDataIn( task, null_port_value ) )
        {
            return true;
        }
        return task->sched_meta->finished;
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
            task->sched_meta->finished = true;
        }
    }

    virtual void reschedule( Task* task )
    {
        auto *t( static_cast< PollingWorkerSchedMeta* >( task->sched_meta ) );
        if( 64 <= ++t->run_count )
        {
            t->run_count = 0;
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
        task->sched_meta->done();
#else
        task->sched_meta->finished = true;
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

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        std::size_t worker_id = task_id;
        task_id += nclones; /* reserve that many task ids */
        tasks_mutex.unlock();
        for( int i( 0 ); nclones > i; ++i )
        {
            /**
             * thread function takes a reference back to the scheduler
             * accessible done boolean flag, essentially when the
             * kernel is done, it can be rescheduled...and this
             * handles that.
             */
            PollingWorker *task = new PollingWorker();
            task->kernel = kernel;
            task->type = POLLING_WORKER;
            task->id = worker_id + i;
            task->clone_id = i;

            //assert( Singleton::allocate()->isReady() );
            Singleton::allocate()->taskInit( task );

            (this)->make_new_sched_meta( task );
        }

        return;
    }

    virtual void make_new_sched_meta( Task *task )
    {
#if USE_UT
        auto *tmeta( new PollingWorkerSchedMeta( task, &wg ) );
        UNUSED( tmeta );
#else
        auto *tmeta( new PollingWorkerSchedMeta( task ) );
        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();
#endif
    }

    /** kernel set **/
    kernelset_t kernels;
    kernelset_t source_kernels;
    kernelset_t sink_kernels;
#if USE_UT
    waitgroup_t wg;
#endif
};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BASIC_HPP */
