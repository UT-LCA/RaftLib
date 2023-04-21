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

#if USE_QTHREAD
using OneShotListNode = struct QThreadListNode;
#else
using OneShotListNode = struct StdThreadListNode;
#endif


class ScheduleOneShot : public ScheduleBasic
{
    typedef OneShotTask OneShotTaskType;
public:
    ScheduleOneShot() : ScheduleBasic()
    {
        src_id = 0;
    }
    virtual ~ScheduleOneShot() = default;

#if UT_FOUND
    virtual void globalInitialize() override
    {
        slab_create( &oneshot_task_slab, "oneshottask",
                     sizeof( OneShotTaskType ), 0 );
        oneshot_task_tcache = slab_create_tcache( &oneshot_task_slab,
                                                  TCACHE_DEFAULT_MAG_SIZE );
    }

    virtual void perthreadInitialize() override
    {
        tcache_init_perthread( oneshot_task_tcache,
                               &__perthread_oneshot_task_pt );
    }
#endif

    static void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *oneshot( static_cast< OneShotTaskType* >( task ) );
            oneshot->is_source = false; /* stop self_iterate() */
        }
    }

    static void reschedule( Task* task )
    {
        /* for source kernels to start a new iteration */
        ScheduleOneShot::self_iterate( task );
        /* for consumer kernels run as tasks */
        ScheduleOneShot::feed_consumers( task );
        Singleton::allocate()->taskCommit( task );
#if USE_UT
        waitgroup_done( task->wg );
#else
        *task->finished = true;
        /* mark finished here because oneshot task does not postexit() */
#endif
    }

protected:

    template< class ONESHOTTYPE >
    static inline ONESHOTTYPE *new_an_oneshot( Kernel *kernel, bool is_source )
    {
#if USE_UT
        auto *task_ptr_tmp( tcache_alloc( &__perthread_oneshot_task_pt ) );
        auto *oneshot( new ( task_ptr_tmp ) ONESHOTTYPE() );
#else
        auto *oneshot( new ONESHOTTYPE() );
#endif
        oneshot->kernel = kernel;
        oneshot->is_source = is_source;

        oneshot->id = task_id.fetch_add( 1, std::memory_order_relaxed );

        return oneshot;
    }

    template< class SCHEDULER, class ONESHOTTYPE >
    static inline void run_oneshot( ONESHOTTYPE *oneshot, int64_t gid )
    {
#if USE_QTHREAD
        oneshot->group_id =
            ( 0 <= gid ) ? oneshot->group_id : oneshot->kernel->getGroup();
#endif

#if USE_UT
        waitgroup_add( &wg, 1 );
        oneshot->wg = &wg;
        rt::Spawn( [ oneshot ]() {
                oneshot->template exe< SCHEDULER >();
                tcache_free( &__perthread_oneshot_task_pt, oneshot ); },
                false, gid );
#else
        auto *tnode( new OneShotListNode(
                    ( SCHEDULER* )nullptr, oneshot, gid ) );
        insert_task_node( tnode );
#endif
    }

    template< class SCHEDULER, class ONESHOTTYPE >
    static inline void start_source_kernel_task( Kernel *kernel )
    {
        auto *task( new_an_oneshot< ONESHOTTYPE >( kernel, true ) );

#if USE_UT || USE_QTHREAD
        int64_t gid = src_id.fetch_add( 1, std::memory_order_relaxed );
#else
        int64_t gid = -1;
#endif

        /* allocate the output buffer */
        Singleton::allocate()->taskInit( task );

        /* run the OneShotTask in a thread */
        run_oneshot< SCHEDULER, ONESHOTTYPE >( task, gid );

        return;
    }

    template< class SCHEDULER, class ONESHOTTYPE >
    static inline void shot_kernel( Kernel *kernel,
                                    const port_key_t &dst_name,
                                    DataRef &ref,
                                    int64_t gid )
    {
        auto *tnext( new_an_oneshot< ONESHOTTYPE >( kernel, false ) );

        /* allocate the output buffer */
        Singleton::allocate()->taskInit( tnext, /* alloc_input = */ true );
        assert( nullptr != tnext->stream_in );
        /* set the input */
        tnext->stream_in->set( dst_name, ref );

        /* run the OneShotTask in a thread */
        run_oneshot< SCHEDULER, ONESHOTTYPE >( tnext, gid );

        return;
    }

    static inline std::atomic< int64_t > src_id;
#if UT_FOUND
    struct slab oneshot_task_slab;
    struct tcache *oneshot_task_tcache;
#endif

private:

    virtual void start_tasks() override
    {
        auto &container( source_kernels );
        for( auto * const k : container )
        {
            start_source_kernel_task< ScheduleOneShot, OneShotTaskType >( k );
        }
    }

    static inline void self_iterate( Task *task )
    {
        auto *oneshot( static_cast< OneShotTaskType* >( task ) );
        if( ! oneshot->is_source )
        {
            return;
        }
        start_source_kernel_task< ScheduleOneShot,
                                  OneShotTaskType >( task->kernel );
    }

    static inline bool feed_consumers( Task *task )
    {
        if( 0 == task->kernel->output.size() )
        {
            return false;
        }
        int64_t gid = -1;
#if USE_QTHREAD
        auto *oneshot( static_cast< OneShotTaskType* >( task ) );
        gid = oneshot->group_id;
#endif
        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        while( Singleton::allocate()->schedPop( task, my_pi, ref, &selected ) )
        {
            auto *other_pi( my_pi->other_port );
            //TODO: deal with a kernel depends on multiple producers
            shot_kernel< ScheduleOneShot, OneShotTaskType >(
                    other_pi->my_kernel, other_pi->my_name, ref, gid );
        }
        return false;
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_ONESHOT_HPP */
