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

#if UT_FOUND
inline __thread tcache_perthread __perthread_oneshot_task_pt;
#endif

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
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        delete oneshot;
    }

    volatile bool is_source;
    std::thread th;
    /* map every task to a kthread */
};

#if QTHREAD_FOUND
struct OneShotQTSchedMeta : public TaskSchedMeta
{
    OneShotQTSchedMeta( Task *the_task, int64_t gid, bool is_s = false ) :
        TaskSchedMeta( the_task ), is_source( is_s )
    {
        task->sched_meta = this;
        group_id = (0 <= gid) ? gid : task->kernel->getGroup();
        qthread_spawn( OneShotQTSchedMeta::run,
                       ( void* ) this,
                       0,
                       0,
                       0,
                       nullptr,
                       group_id,
                       0 );
    }

    virtual ~OneShotQTSchedMeta()
    {
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        delete oneshot;
    }

    static aligned_t run( void *data )
    {
        auto * const tmeta( reinterpret_cast< OneShotQTSchedMeta* >( data ) );
        tmeta->task->exe();
        return 0;
    }

    volatile bool is_source;
    int64_t group_id;
};
#endif

#if UT_FOUND
struct OneShotUTSchedMeta : public TaskSchedMeta
{
    OneShotUTSchedMeta(
            Task *the_task, waitgroup_t *the_wg, bool is_s = false ) :
        TaskSchedMeta( the_task ), wg( the_wg ), is_source( is_s )
    {
        task->sched_meta = this;
        waitgroup_add( wg, 1 );
        rt::Spawn( [ & ](){ task->exe(); } );
    }

    virtual ~OneShotUTSchedMeta()
    {
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        tcache_free( &__perthread_oneshot_task_pt, oneshot );
    }

    waitgroup_t *wg;
    bool is_source;
};
#endif

#if USE_UT
using OneShotSchedMeta = struct OneShotUTSchedMeta;
#elif USE_QTHREAD
using OneShotSchedMeta = struct OneShotQTSchedMeta;
#else
using OneShotSchedMeta = struct OneShotStdSchedMeta;
#endif


class ScheduleOneShot : public ScheduleBasic
{
public:
    ScheduleOneShot() : ScheduleBasic()
    {
    }
    virtual ~ScheduleOneShot() = default;

    virtual bool doesOneShot() const
    {
        return true;
    }

#if UT_FOUND
    virtual void globalInitialize()
    {
        slab_create( &oneshot_task_slab, "oneshottask",
                     sizeof( OneShotTask ), 0 );
        oneshot_task_tcache = slab_create_tcache( &oneshot_task_slab,
                                                  TCACHE_DEFAULT_MAG_SIZE );
    }

    virtual void perthreadInitialize()
    {
        tcache_init_perthread( oneshot_task_tcache,
                               &__perthread_oneshot_task_pt );
    }
#endif

    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        Singleton::allocate()->commit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *tmeta( static_cast< OneShotSchedMeta* >(
                        task->sched_meta ) );
            tmeta->is_source = false; /* stop self_iterate() */
        }
    }

    virtual void reschedule( Task* task )
    {
        feed_consumers( task );
        self_iterate( task ); /* for source kernels to start a new iteration */
#if USE_UT
        auto *tmeta( static_cast< OneShotSchedMeta* >( task->sched_meta ) );
        waitgroup_done( tmeta->wg );
        delete tmeta;
#else
        task->sched_meta->finished = true;
        /* mark finished here because oneshot task does not postexit() */
#endif
    }

protected:

    void start_tasks()
    {
        auto &container( source_kernels );
        for( auto * const k : container )
        {
            start_source_kernel_task( k );
        }
    }

    void start_source_kernel_task( Kernel *kernel )
    {
#if USE_UT
        auto *task_ptr_tmp = tcache_alloc( &__perthread_oneshot_task_pt );
        OneShotTask *task = new ( task_ptr_tmp ) OneShotTask();
#else
        OneShotTask *task = new OneShotTask();
#endif
        task->kernel = kernel;
        task->type = ONE_SHOT;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        task->id = task_id++;
#if USE_QTHREAD
        src_id++;
#endif
        tasks_mutex.unlock();

        /* allocate the output buffer */
        Singleton::allocate()->taskInit( task );

#if USE_UT
        auto *tmeta( new OneShotSchedMeta( task, &wg, true ) );
        UNUSED( tmeta );
#else
        auto *tmeta( new OneShotSchedMeta( task,
#if USE_QTHREAD
                                           src_id,
#endif /* END USE_QTHREAD */
                                           true ) );
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

        return;
    }

    void feed_consumers( Task *task )
    {
        auto *t( static_cast< OneShotTask* >( task ) );
        Kernel *mykernel( task->kernel );
        if( 0 == mykernel->output.size() )
        {
            return;
        }
        if( 1 == mykernel->output.size() )
        {
            if( ! t->stream_out->isSent() )
            {
                return;
            }
            const auto *other_pi(
                    mykernel->output.begin()->second.other_port );
#if USE_QTHREAD
            auto *tmeta(
                    static_cast< OneShotSchedMeta* >( task->sched_meta ) );
            shot_kernel( other_pi->my_kernel, t->stream_out, tmeta->group_id );
#else
            shot_kernel( other_pi->my_kernel, t->stream_out );
#endif
            DataRef ref;
            while( ( ref = Singleton::allocate()->portPop( other_pi ) ) )
            {
#if USE_QTHREAD
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref,
                             tmeta->group_id );
#else
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref );
#endif
            }
            return;
        }
        for( auto &p : t->stream_out->getUsed() )
        {
            const auto *other_pi( mykernel->output[ p.first ].other_port );
            //TODO: deal with a kernel depends on multiple producers
#if USE_QTHREAD
            auto *tmeta(
                    static_cast< OneShotSchedMeta* >( task->sched_meta ) );
            shot_kernel( other_pi->my_kernel, other_pi->my_name, p.second,
                         tmeta->group_id );
#else
            shot_kernel( other_pi->my_kernel, other_pi->my_name, p.second );
#endif
            DataRef ref;
            while( ( ref = Singleton::allocate()->portPop( other_pi ) ) )
            {
#if USE_QTHREAD
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref,
                             tmeta->group_id );
#else
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref );
#endif
            }
        }
    }

    void self_iterate( Task *task )
    {
        auto *tmeta( static_cast< OneShotSchedMeta* >(
                     task->sched_meta ) );
        if( ! tmeta->is_source )
        {
            return;
        }
        start_source_kernel_task( task->kernel );
    }

    void shot_kernel( Kernel *kernel,
                      StreamingData *src_sd,
                      int64_t gid = -1 )
    {
#if USE_UT
        auto *task_ptr_tmp = tcache_alloc( &__perthread_oneshot_task_pt );
        OneShotTask *tnext = new ( task_ptr_tmp ) OneShotTask();
#else
        OneShotTask *tnext = new OneShotTask();
#endif
        tnext->kernel = kernel;
        tnext->type = ONE_SHOT;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        tnext->id = task_id++;
        tasks_mutex.unlock();

        Singleton::allocate()->taskInit( tnext );
        tnext->stream_in = src_sd->out2in1piece();

        UNUSED( gid );
#if USE_UT
        auto *tmeta( new OneShotSchedMeta( tnext, &wg ) );
        UNUSED( tmeta );
#else
#if USE_QTHREAD
        auto *tmeta( new OneShotSchedMeta( tnext, gid ) );
#else
        auto *tmeta( new OneShotSchedMeta( tnext ) );
#endif /* END USE_QTHREAD */
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

    void shot_kernel( Kernel *kernel,
                      const port_key_t &dst_name,
                      DataRef &ref,
                      int64_t gid = -1 )
    {
#if USE_UT
        auto *task_ptr_tmp = tcache_alloc( &__perthread_oneshot_task_pt );
        OneShotTask *tnext = new ( task_ptr_tmp ) OneShotTask();
#else
        OneShotTask *tnext = new OneShotTask();
#endif
        tnext->kernel = kernel;
        tnext->type = ONE_SHOT;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        tnext->id = task_id++;
        tasks_mutex.unlock();

        Singleton::allocate()->taskInit( tnext, true );
        tnext->stream_in->set( dst_name, ref );

        UNUSED( gid );
#if USE_UT
        auto *tmeta( new OneShotSchedMeta( tnext, &wg ) );
        UNUSED( tmeta );
#else
#if USE_QTHREAD
        auto *tmeta( new OneShotSchedMeta( tnext, gid ) );
#else
        auto *tmeta( new OneShotSchedMeta( tnext ) );
#endif /* END USE_QTHREAD */
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

    volatile int64_t src_id = 0;
#if UT_FOUND
    struct slab oneshot_task_slab;
    struct tcache *oneshot_task_tcache;
#endif
};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP */
