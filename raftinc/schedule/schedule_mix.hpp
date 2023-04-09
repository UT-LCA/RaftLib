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


class ScheduleMix : public ScheduleBurst
{
public:
    ScheduleMix() : ScheduleBurst()
    {
    }
    virtual ~ScheduleMix() = default;

    virtual bool shouldExit( Task* task )
    {
        if( ONE_SHOT != task->type &&
            ! Singleton::allocate()->taskHasInputPorts( task ) &&
            ! Singleton::allocate()->dataInReady( task, null_port_value ) )
        {
            return true;
        }
        return task->stopped;
    }

    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
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
                auto *burst( static_cast< BurstTask* >( task ) );
                burst->is_source = false; /* stop self_iterate() */
            }
        }
    }

    virtual void reschedule( Task* task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *worker( static_cast< PollingWorker* >( task ) );
            (this)->feed_consumers( worker );
            if( 64 <= ++worker->poll_count )
            {
                worker->poll_count = 0;
                raft::yield();
            }
            return;
        }

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
        Singleton::allocate()->invalidateOutputs( task );
#if USE_UT
        waitgroup_done( task->wg );
#else
        *task->finished = true;
#endif
    }

protected:

    virtual void start_tasks() override
    {
        ScheduleBasic::start_tasks();
    }

    virtual int get_nclones( Kernel * const kernel ) override
    {
#if IGNORE_HINT_0CLONE
        return std::max( 1, kernel->getCloneFactor() );
#else
        return kernel->getCloneFactor();
#endif
    }

    virtual bool feed_consumers( Task *task ) override
    {
        if( 0 == task->kernel->output.size() )
        {
            return false;
        }
        int64_t gid = -1;
#if USE_QTHREAD
        //gid = burst->group_id;
#endif
        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        bool is_last = false;
        int cnt = 0;
        while( Singleton::allocate()->schedPop(
                    task, my_pi, ref, &selected, &is_last ) )
        {
            cnt++;
            auto *other_pi( my_pi->other_port );
            if( is_last && ONE_SHOT == task->type )
            {
                /* reload the BurstTask with the consumer kernel */
                auto *burst( static_cast< BurstTask* >( task ) );
                Singleton::allocate()->taskCommit( burst );
                /* free up old stream_in/out before overwritten */
                burst->is_source = false;
                burst->kernel = other_pi->my_kernel;
                Singleton::allocate()->taskInit( burst, true );
                burst->stream_in->set( other_pi->my_name, ref );
                return true;
            }
            else
            {
                //TODO: deal with a kernel depends on multiple producers
                shot_kernel( other_pi->my_kernel, other_pi->my_name, ref,
                             gid );
            }
        }
        return false;
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_MIX_HPP */
