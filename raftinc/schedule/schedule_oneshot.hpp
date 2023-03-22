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
#include "raftinc/signalhandler.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/kernelkeeper.tcc"
#include "raftinc/sysschedutil.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/task.hpp"
#include "raftinc/schedule/schedule_basic.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/oneshottask.hpp"

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

    virtual ~OneShotStdSchedMeta()
    {
        th.join();
    }

    volatile bool is_source;
    std::thread th;
    /* map every task to a kthread */
};

#if QTHREAD_FOUND
struct OneShotQTSchedMeta : public TaskSchedMeta
{
    OneShotQTSchedMeta( Task *the_task, bool is_s = false ) :
        TaskSchedMeta( the_task ), is_source( is_s )
    {
        task->sched_meta = this;
        qthread_spawn( OneShotQTSchedMeta::run,
                       ( void* ) this,
                       0,
                       0,
                       0,
                       nullptr,
                       task->kernel->getGroup() % qthread_num_shepherds(),
                       0 );
    }

    virtual ~OneShotQTSchedMeta()
    {
    }

    static aligned_t run( void *data )
    {
        auto * const tmeta( reinterpret_cast< OneShotQTSchedMeta* >( data ) );
        tmeta->task->exe();
        return 0;
    }

    volatile bool is_source;
};
#endif

#if USE_QTHREAD
using OneShotSchedMeta = struct OneShotQTSchedMeta;
#else
using OneShotSchedMeta = struct OneShotStdSchedMeta;
#endif


class ScheduleOneShot : public ScheduleBasic
{
public:
    ScheduleOneShot( DAG &dag, Allocate *the_alloc ) :
        ScheduleBasic( dag, the_alloc )
    {
    }
    virtual ~ScheduleOneShot() = default;

    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        Singleton::allocate()->commit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *tmeta( static_cast< OneShotStdSchedMeta* >(
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

    void start_tasks()
    {
        auto &container( source_kernels.acquire() );
        for( auto * const k : container )
        {
            start_source_kernel_task( k );
        }
        source_kernels.release();
    }

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
        Singleton::allocate()->taskInit( task );

        auto *tmeta( new OneShotSchedMeta( task, true ) );
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
        auto *t( static_cast< OneShotTask* >( task ) );
        Kernel *mykernel( task->kernel );
        for( auto &p : t->stream_out->getUsed() )
        {
            const auto *other_pi( mykernel->output[ p.first ].other_port );
            //TODO: deal with a kernel depends on multiple producers
            shot_kernel( other_pi->my_kernel, other_pi->my_name, p.second );
            DataRef ref;
            while( ( ref = Singleton::allocate()->portPop( other_pi ) ) )
            {
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref );
            }
        }
    }

    void self_iterate( Task *task )
    {
        auto *tmeta( static_cast< OneShotStdSchedMeta* >(
                     task->sched_meta ) );
        if( ! tmeta->is_source )
        {
            return;
        }
        start_source_kernel_task( task->kernel );
    }

    void shot_kernel( Kernel *kernel,
                      const port_key_t &dst_name,
                      DataRef &ref )
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

        tnext->stream_in = new StreamingData( tnext, StreamingData::IN );
        tnext->stream_in->set( dst_name, ref );
        Singleton::allocate()->taskInit( tnext );

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        auto *tmeta( new OneShotSchedMeta( tnext ) );
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP */
