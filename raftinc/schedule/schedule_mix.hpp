/**
 * schedule_mix.hpp -
 * @author: Qinzhe Wu
 * @version: Fri Apr 07 15:20:00 2023
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
#ifndef RAFT_SCHEDULE_SCHEDULE_MIX_HPP
#define RAFT_SCHEDULE_SCHEDULE_MIX_HPP  1
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
#include "raftinc/schedule/schedule_burst.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/oneshottask.hpp"

namespace raft {

/* forward declaration to make friends */
class ScheduleMixCV;

class ScheduleMix : public ScheduleBurst
{
    typedef PollingWorker WorkerTaskType;
    typedef BurstTask OneShotTaskType;
public:
    ScheduleMix() : ScheduleBurst()
    {
    }
    virtual ~ScheduleMix() = default;

    static bool shouldExit( Task* task )
    {
        if( ONE_SHOT != task->type &&
            ! Singleton::allocate()->taskHasInputPorts( task ) &&
            ! Singleton::allocate()->dataInReady( task, null_port_value ) )
        {
            return true;
        }
        return task->stopped;
    }

    static void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            if( ONE_SHOT != task->type )
            {
                task->stopped = true;
            }
            else
            {
                auto *burst( static_cast< OneShotTaskType* >( task ) );
                burst->is_source = false; /* stop self_iterate() */
            }
        }
    }

    static void reschedule( Task* task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *worker( static_cast< WorkerTaskType* >( task ) );
            if( ! ScheduleMix::feed_consumers( worker ) )
            {
                ScheduleBasic::worker_yield( worker );
            }
            return;
        }

        /* when ONE_SHOT == task->type, can simply downgrade to follow
         * the path of ScheduleBurst */
        auto *burst( static_cast< OneShotTaskType* >( task ) );
        if( ! ScheduleBurst::try_reload( burst ) )
        {
            Singleton::allocate()->taskCommit( task );
            task->stopped = true;
        }
    }

protected:

    static inline int get_nclones( Kernel * const kernel )
    {
#if ARMQ_NO_HINT_0CLONE
        return std::max( 1, kernel->getCloneFactor() );
#else
        return kernel->getCloneFactor();
#endif
    }

private:

    /* Note: s/ScheduleBasic/ScheduleMix/g of ScheduleBasic::start_tasks() */
    virtual void start_tasks() override
    {
        auto &container( kernels );
#if USE_UT
        std::size_t ntasks = 0;
        for( auto * const k : container )
        {
            ntasks += ScheduleMix::get_nclones( k );
        }
        waitgroup_add( &wg, ntasks );
#endif
        for( auto * const k : container )
        {
            start_worker< ScheduleMix, WorkerTaskType >(
                    k, ScheduleMix::get_nclones( k ) );
        }
    }

    static inline bool feed_consumers( WorkerTaskType *worker )
    {
        if( 0 == worker->kernel->output.size() )
        {
            return false;
        }

        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        bool is_last = false;
        while( Singleton::allocate()->schedPop(
                    worker, my_pi, ref, &selected, &is_last ) )
        {
#if ARMQ_NO_HINT_0CLONE && ARMQ_NO_HINT_FULLQ
            BUG(); /* when both hints ignored, should never spawn OneShot */
#endif
            auto *other_pi( my_pi->other_port );
            if( is_last )
            {
#if USE_UT && ! ARMQ_NO_INSTANT_SWAP
                /* POLLING_WORKER/CONDVAR_WORKER, jump to new task directly */
                ScheduleMix::shot_direct( other_pi, ref );
                return true;
#else
                shot_kernel< ScheduleMix, OneShotTaskType > (
                        other_pi->my_kernel, other_pi->my_name, ref, -1 );
                break;
#endif
            }
            else
            {
                //TODO: deal with a kernel depends on multiple producers
                shot_kernel< ScheduleMix, OneShotTaskType >(
                        other_pi->my_kernel, other_pi->my_name, ref, -1 );
            }
        }
        return false;
    }

#if UT_FOUND
    static inline void shot_direct( const PortInfo *other_pi,
                                    DataRef &ref )
    {
        auto *oneshot( new_an_oneshot< OneShotTaskType >( other_pi->my_kernel,
                                                          false ) );

        Singleton::allocate()->taskInit( oneshot, true );
        oneshot->stream_in->set( other_pi->my_name, ref );

        waitgroup_add( &wg, 1 );
        oneshot->wg = &wg;
        rt::Spawn( [ oneshot ]() {
                oneshot->exe< ScheduleMix >();
                free_an_oneshot( oneshot ); },
                   /* swap = */ true );
    }
#endif

    friend ScheduleMixCV; /* to allow to use feed_consumers() */
};

/**
 * ScheduleMixCV - a special scheduler that is almost identical to
 * ScheduleMix in most part, except this one exploit the producer-consumer
 * relationship in DAG topology to accurately schedule the tasks getting
 * data ready to make progress.
 * Note: should be used together with AllocateMixCV.
 */
class ScheduleMixCV : public ScheduleMix
{
    typedef CondVarWorker WorkerTaskType;
    typedef BurstTask OneShotTaskType;
public:
    ScheduleMixCV() : ScheduleMix()
    {
    }
    virtual ~ScheduleMixCV() = default;

    static void prepare( Task *task )
    {
        Singleton::allocate()->registerConsumer( task );
    }

    static void reschedule( Task *task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *worker( static_cast< WorkerTaskType* >( task ) );
            if( ! ScheduleMix::feed_consumers( worker ) )
            {
                if( 0 == ScheduleMixCV::get_nclones( worker->kernel ) )
                {
                    worker->wait< ScheduleMixCV >();
                }
                else
                {
                    worker_yield( worker );
                }
            }
            return;
        }

        /* when ONE_SHOT == task->type, can simply downgrade to follow
         * the path of ScheduleBurst */
        auto *burst( static_cast< OneShotTaskType* >( task ) );
        if( ! ScheduleBurst::try_reload( burst ) )
        {
            Singleton::allocate()->taskCommit( task );
            task->stopped = true;
        }
    }

private:

    /* Note: s/ScheduleBasic/ScheduleMixCV/g of ScheduleBasic::start_tasks() */
    virtual void start_tasks() override
    {
        auto &container( kernels );
#if USE_UT
        std::size_t ntasks = 0;
        for( auto * const k : container )
        {
            ntasks += ScheduleMixCV::get_nclones( k );
        }
        waitgroup_add( &wg, ntasks );
#endif
        for( auto * const k : container )
        {
            start_worker< ScheduleMixCV, WorkerTaskType >(
                    k, ScheduleMixCV::get_nclones( k ) );
        }
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_MIX_HPP */
