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
#include <atomic>

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

#if USE_QTHREAD /* group_id not defined in OneShotTask unless USE_QTHREAD */
struct OneShotQTListNode : public TaskListNode
{
    OneShotQTListNode( Task *the_task ) : TaskListNode( the_task )
    {
        (this)->task->finished = &finished;
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        qthread_spawn( QThreadListNode::run,
                       ( void* ) task,
                       0,
                       0,
                       0,
                       nullptr,
                       (0 <= oneshot->group_id) ? oneshot->group_id :
                                                  task->kernel->getGroup(),
                       0 );
    }

    virtual ~OneShotQTListNode()
    {
        delete task;
    }
};
#endif

#if USE_QTHREAD
using OneShotListNode = struct OneShotQTListNode;
#else
using OneShotListNode = struct StdThreadListNode;
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
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *oneshot( static_cast< OneShotTask* >( task ) );
            oneshot->is_source = false; /* stop self_iterate() */
        }
    }

    virtual void reschedule( Task* task )
    {
        self_iterate( task ); /* for source kernels to start a new iteration */
        feed_consumers( task );
        Singleton::allocate()->taskCommit( task );
#if USE_UT
        waitgroup_done( task->wg );
#else
        *task->finished = true;
        /* mark finished here because oneshot task does not postexit() */
#endif
    }

protected:

    virtual void start_tasks()
    {
        auto &container( source_kernels );
        for( auto * const k : container )
        {
            (this)->start_source_kernel_task( k );
        }
    }

    virtual void start_source_kernel_task( Kernel *kernel )
    {
        auto *task( (this)->new_an_oneshot() );
        task->is_source = true;
        task->kernel = kernel;

        int64_t gid = -1;
        task->id = task_id.fetch_add( 1, std::memory_order_relaxed );
#if USE_QTHREAD
        gid = src_id.fetch_add( 1, std::memory_order_relaxed );
#endif

        /* allocate the output buffer */
        Singleton::allocate()->taskInit( task );

        run_oneshot( task, gid );

        return;
    }

    virtual bool feed_consumers( Task *task )
    {
        if( 0 == task->kernel->output.size() )
        {
            return false;
        }
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        int64_t gid = -1;
#if USE_QTHREAD
        gid = oneshot->group_id;
#endif
        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        while( Singleton::allocate()->schedPop( task, my_pi, ref, &selected ) )
        {
            auto *other_pi( my_pi->other_port );
            //TODO: deal with a kernel depends on multiple producers
            shot_kernel( other_pi->my_kernel, other_pi->my_name, ref, gid );
        }
        auto *stream_out( oneshot->stream_out );
        if( stream_out->is1Piece() )
        {
            if( stream_out->isSingle() )
            {
                if( stream_out->isSent() )
                {
                    my_pi = &oneshot->kernel->output.begin()->second;
                    const auto *other_pi( my_pi->other_port );
                    shot_kernel( other_pi->my_kernel, oneshot->stream_out,
                                 gid );
                }
            }
            else
            {
                for( auto &p : oneshot->stream_out->getUsed() )
                {
                    const auto *other_pi(
                            oneshot->kernel->output[ p.first ].other_port );
                    shot_kernel( other_pi->my_kernel, other_pi->my_name,
                                 p.second, gid );
                }
            }
        }
        return false;
    }

    virtual void self_iterate( Task *task )
    {
        auto *oneshot( static_cast< OneShotTask* >( task ) );
        if( ! oneshot->is_source )
        {
            return;
        }
        start_source_kernel_task( task->kernel );
    }

    virtual void shot_kernel( Kernel *kernel,
                              StreamingData *src_sd,
                              int64_t gid )
    {
        auto *tnext( (this)->new_an_oneshot() );
        tnext->is_source = false;
        tnext->kernel = kernel;

        tnext->id = task_id.fetch_add( 1, std::memory_order_relaxed );

        Singleton::allocate()->taskInit( tnext );
        tnext->stream_in = src_sd->out2in1piece();

        run_oneshot( tnext, gid );
    }

    virtual void shot_kernel( Kernel *kernel,
                              const port_key_t &dst_name,
                              DataRef &ref,
                              int64_t gid )
    {
        auto *tnext( (this)->new_an_oneshot() );
        tnext->is_source = false;
        tnext->kernel = kernel;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        tnext->id = task_id.fetch_add( 1, std::memory_order_relaxed );
        tasks_mutex.unlock();

        Singleton::allocate()->taskInit( tnext, true );
        tnext->stream_in->set( dst_name, ref );

        run_oneshot( tnext, gid );
    }

    std::atomic< int64_t > src_id = { 0 };
#if UT_FOUND
    struct slab oneshot_task_slab;
    struct tcache *oneshot_task_tcache;
#endif

    virtual OneShotTask *new_an_oneshot()
    {
#if USE_UT
        auto *task_ptr_tmp( tcache_alloc( &__perthread_oneshot_task_pt ) );
        OneShotTask *oneshot( new ( task_ptr_tmp ) OneShotTask() );
#else
        OneShotTask *oneshot( new OneShotTask() );
#endif
        return oneshot;
    }

    virtual void run_oneshot( OneShotTask *oneshot, int64_t gid )
    {
        UNUSED( gid );
#if USE_UT
        waitgroup_add( &wg, 1 );
        oneshot->wg = &wg;
        rt::Spawn( [ oneshot ]() {
                oneshot->exe();
                tcache_free( &__perthread_oneshot_task_pt, oneshot ); } );
#else
#if USE_QTHREAD
        oneshot->group_id = gid;
#endif
        auto *tnode( new OneShotListNode( oneshot ) );
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

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP */
