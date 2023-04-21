/**
 * schedule_burst.hpp -
 * @author: Qinzhe Wu
 * @version: Mon Apr 03 10:34:00 2023
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
#ifndef RAFT_SCHEDULE_SCHEDULE_BURST_HPP
#define RAFT_SCHEDULE_SCHEDULE_BURST_HPP  1
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
#include "raftinc/schedule/schedule_oneshot.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/oneshottask.hpp"

namespace raft {

/* forward declaration to make friends */
class ScheduleMix;
class ScheduleMixCV;

class ScheduleBurst : public ScheduleOneShot
{
    typedef BurstTask OneShotTaskType;
public:
    ScheduleBurst() : ScheduleOneShot()
    {
    }
    virtual ~ScheduleBurst() = default;

//#if UT_FOUND
//    /* Note: copy of ScheduleOneShot::globalInitialize() */
//    virtual void globalInitialize() override
//    {
//        slab_create( &oneshot_task_slab, "oneshottask",
//                     sizeof( OneShotTaskType ), 0 );
//        oneshot_task_tcache = slab_create_tcache( &oneshot_task_slab,
//                                                  TCACHE_DEFAULT_MAG_SIZE );
//    }
//#endif

    static bool shouldExit( Task* task )
    {
        return task->stopped;
    }

    static void reschedule( Task* task )
    {
        if( ! ScheduleBurst::try_reload( task ) )
        {
            Singleton::allocate()->taskCommit( task );
            task->stopped = true;
        }
    }

private:

    /* Note: s/ScheduleOneShot/ScheduleBurst/g of
     * ScheduleOneShot::start_tasks() */
    virtual void start_tasks() override
    {
        auto &container( source_kernels );
        for( auto * const k : container )
        {
            start_source_kernel_task< ScheduleBurst, OneShotTaskType >( k );
        }
    }

    static inline bool try_reload( Task *task )
    {
        auto *burst( static_cast< OneShotTaskType* >( task ) );
        auto *source_kernel_cpy( burst->is_source ? burst->kernel : nullptr );
        bool reloaded( false );

#if USE_QTHREAD
        int64_t gid = burst->group_id;
#else
        int64_t gid = -1;
#endif

        /* try feeding consumers */
        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        bool is_last = false;
        while( Singleton::allocate()->schedPop(
                    task, my_pi, ref, &selected, &is_last ) )
        {
            auto *other_pi( my_pi->other_port );
            if( ! is_last )
            {
                //TODO: deal with a kernel depends on multiple producers
                shot_kernel< ScheduleBurst, OneShotTaskType >(
                        other_pi->my_kernel, other_pi->my_name, ref, gid );
            }
            else
            {
                Singleton::allocate()->taskCommit( burst );
                /* free up old stream_in/out before overwritten */
                burst->is_source = false;
                burst->kernel = other_pi->my_kernel;
                Singleton::allocate()->taskInit( burst, true );
                burst->stream_in->set( other_pi->my_name, ref );
                reloaded = true;
                break;
            }
        }

        /*  we need to reiterate source kernel */
        if( nullptr != source_kernel_cpy )
        {
            if( reloaded ) /* burst reloaded with a consumer already */
            {
                start_source_kernel_task< ScheduleBurst, OneShotTaskType >(
                        source_kernel_cpy );
            }
            else /* burst free to reload with source_kernel */
            {
                reloaded = true;
            }
        }

        return reloaded;
    }

    friend ScheduleMix;
    friend ScheduleMixCV;
    /* allow them to reuse try_reload() */

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BURST_HPP */
