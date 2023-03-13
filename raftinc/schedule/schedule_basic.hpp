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
#include "signalhandler.hpp"
#include "rafttypes.hpp"
#include "defs.hpp"
#include "kernel.hpp"
#include "kernelkeeper.tcc"
#include "sysschedutil.hpp"
#include "dag.hpp"
#include "task.hpp"
#include "allocate/allocate.hpp"
#include "pollingworker.hpp"
#include <affinity>

namespace raft {

struct StdThreadSchedMeta : public TaskSchedMeta
{
    StdThreadSchedMeta( Task *the_task ) :
        TaskSchedMeta( the_task ), th( [ & ](){
                (this)->task->sched_meta = this;
                (this)->task->exe(); } )
    {
    }

    std::thread th;
    /* map every task to a kthread */
};


class ScheduleBasic : public Schedule
{
public:
    ScheduleBasic( DAG &dag, Allocate *the_alloc ) :
        Schedule(), kernels( dag.getKernels() ),
        source_kernels( dag.getSourceKernels() ),
        sink_kernels( dag.getSinkKernels() ),
        alloc( the_alloc )
    {
    }
    virtual ~ScheduleBasic() = default;

    /**
     * schedule - called to start execution of all
     * kernels.  Implementation specific so it
     * is purely virtual.
     */
    virtual void schedule()
    {
        while( ! alloc->isReady() )
        {
            raft::yield();
        }

        auto &container( kernels.acquire() );
        for( auto * const k : container )
        {
            start_polling_worker( k );
        }
        kernels.release();

        bool keep_going( true );
        while( keep_going )
        {
            while( ! tasks_mutex.try_lock() )
            {
                raft::yield();
            }
            //exit, we have a lock
            keep_going = false;
            TaskSchedMeta* tparent( tasks );
            //loop over each thread and check if done
            while( nullptr != tparent->next )
            {
                auto *tmeta( reinterpret_cast< StdThreadSchedMeta* >(
                            tparent->next ) );
                if( tmeta->finished )
                {
                    tmeta->th.join();
                    tparent->next = tmeta->next;
                    delete tmeta;
                }
                else /* a task ! finished */
                {
                    tparent = tmeta;
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
        return ( task->kernel->pop( task, true ) &&
                 task->kernel->allocate( task, true ) );
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
        raft::yield();
    }

    virtual void prepare( Task* task )
    {
    }

    virtual void postexit( Task* task )
    {
        auto *worker( reinterpret_cast< PollingWorker* >( task ) );
        Singleton::allocate()->invalidateOutputs( task );
        task->sched_meta->finished = true;
    }

protected:

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

            // this scheduler assume 1 pollingworker per kernel
            // so we just take the per-port FIFO as the task FIFO
            //assert( Singleton::allocate()->isReady() );
            Singleton::allocate()->taskAllocate( task );

            auto *tmeta( new StdThreadSchedMeta( task ) );

            while( ! tasks_mutex.try_lock() )
            {
                raft::yield();
            }
            /* insert into tasks linked list */
            tmeta->next = tasks->next;
            tasks->next = tmeta;
            /** we got here, unlock **/
            tasks_mutex.unlock();
        }

        return;
    }

    /** kernel set **/
    kernelkeeper kernels;
    kernelkeeper source_kernels;
    kernelkeeper sink_kernels;

    Allocate *alloc;
};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BASIC_HPP */
