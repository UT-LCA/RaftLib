/**
 * schedule_oneshot.hpp -
 * @author: Qinzhe Wu
 * @version: Thu Mar 09 18:22:00 2023
 *
 * Copyright 2023 The Regents of the University of Texas
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
#ifndef RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP
#define RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP  1
#include "signalhandler.hpp"
#include "rafttypes.hpp"
#include "defs.hpp"
#include "kernel.hpp"
#include "kernelkeeper.tcc"
#include "sysschedutil.hpp"
#include "dag.hpp"
#include "task.hpp"
#include "schedule_basic.hpp"
#include "allocate/allocate.hpp"
#include "oneshottask.hpp"

namespace raft {

/* cannot just inherit from StdThreadSchedMeta because is_source
 * must be initialized before th thread start executing */
struct OneShotStdSchedMeta : public TaskSchedMeta
{
    OneShotStdSchedMeta( Task *the_task, bool is_s = false ) :
        TaskSchedMeta( the_task ), is_source( is_s ), th( [ & ](){
                (this)->task->sched_meta = this;
                (this)->task->exe(); } )
    {
    }

    volatile bool is_source;
    std::thread th;
    /* map every task to a kthread */
};


class ScheduleOneShot : public ScheduleBasic
{
public:
    ScheduleOneShot( DAG &dag, Allocate *the_alloc ) :
        ScheduleBasic( dag, the_alloc )
    {
    }
    virtual ~ScheduleOneShot() = default;

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

        auto &container( source_kernels.acquire() );
        for( auto * const k : container )
        {
            start_source_kernel_task( k );
        }
        source_kernels.release();

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
                auto *tmeta( reinterpret_cast< OneShotStdSchedMeta* >(
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

    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        Singleton::allocate()->commit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *tmeta(
                    reinterpret_cast< OneShotStdSchedMeta* >(
                        task->sched_meta ) );
            tmeta->is_source = false; /* stop self_iterate() */
        }
    }

    virtual void reschedule( Task* task )
    {
        feed_consumers( task );
        self_iterate( task ); /* for source kernels to start a new iteration */
        task->sched_meta->finished = true;
        /* mark finished here because oneshot task doest not postexit() */
    }

protected:

    void start_source_kernel_task( Kernel *kernel )
    {
        OneShotTask *task = new OneShotTask();
        task->kernel = kernel;
        task->type = ONE_SHOT;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        task->id = task_id++;
        tasks_mutex.unlock();

        /* allocate the output buffer */
        task->stream_out = &Singleton::allocate()->getBufOut( task );

        auto *tmeta( new OneShotStdSchedMeta( task, true ) );
        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();

        return;
    }

    void feed_consumers( Task *task )
    {
        auto *t( reinterpret_cast< OneShotTask* >( task ) );
        Kernel *mykernel( task->kernel );
        auto *tmeta( reinterpret_cast< OneShotStdSchedMeta* >(
                    task->sched_meta ) );
        for( auto &[ name, ref ] : *t->stream_out )
        {
            const auto *other_pi( mykernel->output[ name ].other_port );
            //TODO: deal with a kernel depends on multiple producers
            shot_kernel( other_pi->my_kernel, task, name, other_pi->my_name );
        }
    }

    void self_iterate( Task *task )
    {
        static int cnt = 0;
        auto *tmeta( reinterpret_cast< OneShotStdSchedMeta* >(
                     task->sched_meta ) );
        if( ! tmeta->is_source )
        {
            return;
        }
        start_source_kernel_task( task->kernel );
    }

    void shot_kernel( Kernel *kernel, Task *task, const port_name_t &src_name,
                      const port_name_t &dst_name )
    {
        OneShotTask *tnext = new OneShotTask();
        tnext->kernel = kernel;
        tnext->type = ONE_SHOT;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        tnext->id = task_id++;
        tasks_mutex.unlock();

        auto *oneshot( reinterpret_cast< OneShotTask* >( task ) );
        /* TODO: inform allocate to move the output of task to tnext */
        tnext->stream_in = new StreamingData();
        tnext->stream_in->set( dst_name, oneshot->stream_out->get( src_name ) );
        tnext->stream_out = &Singleton::allocate()->getBufOut( tnext );

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        auto *tmeta( new OneShotStdSchedMeta( tnext ) );
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP */
