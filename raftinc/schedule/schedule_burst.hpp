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


class ScheduleBurst : public ScheduleOneShot
{
public:
    ScheduleBurst() : ScheduleOneShot()
    {
    }
    virtual ~ScheduleBurst() = default;

    virtual bool shouldExit( Task* task )
    {
        return task->stopped;
    }

    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            auto *burst( static_cast< BurstTask* >( task ) );
            burst->is_source = false; /* stop self_iterate() */
        }
    }

    virtual void reschedule( Task* task )
    {
        self_iterate( task ); /* for source kernels to start a new iteration */
        bool reloaded( (this)->feed_consumers( task ) );
        if( ! reloaded )
        {
            Singleton::allocate()->taskCommit( task );
            task->stopped = true;
        }
    }

    virtual void postexit( Task* task )
    {
#if USE_UT
        waitgroup_done( task->wg );
#else
        *task->finished = true;
#endif
    }

protected:

    virtual bool feed_consumers( Task *task )
    {
        if( 0 == task->kernel->output.size() )
        {
            return false;
        }
        auto *burst( static_cast< BurstTask* >( task ) );
        int64_t gid = -1;
#if USE_QTHREAD
        gid = burst->group_id;
#endif
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
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref,
                             gid );
            }
            else
            {
                burst->is_source = false;
                burst->kernel = other_pi->my_kernel;
                Singleton::allocate()->taskCommit( burst );
                /* free up old stream_in/out before overwritten */
                Singleton::allocate()->taskInit( burst, true );
                burst->stream_in->set( other_pi->my_name, ref );
                return true;
            }
        }
        auto *stream_out( burst->stream_out );
        if( stream_out->is1Piece() )
        {
            if( stream_out->isSingle() )
            {
                if( stream_out->isSent() )
                {
                    my_pi = &burst->kernel->output.begin()->second;
                    const auto *other_pi( my_pi->other_port );
                    burst->is_source = false;
                    burst->kernel = other_pi->my_kernel;
                    auto *stream_in_tmp( burst->stream_out->out2in1piece() );
                    Singleton::allocate()->taskCommit( burst );
                    /* free up old stream_in/out before overwritten */
                    Singleton::allocate()->taskInit( burst );
                    burst->stream_in = stream_in_tmp;
                    return true;
                }
            }
            else
            {
                for( auto &p : burst->stream_out->getUsed() )
                {
                    const auto *other_pi(
                            burst->kernel->output[ p.first ].other_port );
                    shot_kernel( other_pi->my_kernel, other_pi->my_name,
                                 p.second, gid );
                }
            }
        }
        return false;
    }

    virtual OneShotTask *new_an_oneshot()
    {
#if USE_UT
        auto *task_ptr_tmp( tcache_alloc( &__perthread_oneshot_task_pt ) );
        BurstTask *oneshot( new ( task_ptr_tmp ) BurstTask() );
#else
        BurstTask *oneshot( new BurstTask() );
#endif
        return oneshot;
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BURST_HPP */
