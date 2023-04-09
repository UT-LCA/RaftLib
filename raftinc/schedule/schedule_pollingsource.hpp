/**
 * schedule_pollingsource.hpp - let the source kernel tasks mimic the
 * PollingWorker scheduling with BurstTask.
 * @author: Qinzhe Wu
 * @version: Tue Apr 04 00:20:00 2023
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
#ifndef RAFT_SCHEDULE_SCHEDULE_POLLINGSOURCE_HPP
#define RAFT_SCHEDULE_SCHEDULE_POLLINGSOURCE_HPP  1
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


class SchedulePollingSource : public ScheduleBurst
{
public:
    SchedulePollingSource() : ScheduleBurst()
    {
    }
    virtual ~SchedulePollingSource() = default;

    virtual void reschedule( Task* task ) override
    {
        auto *burst( static_cast< BurstTask* >( task ) );
        if( burst->is_source )
        {
            pollingsource_feed_consumers( burst );
        }
        else
        {
            bool reloaded( (this)->feed_consumers( task ) );
            if( ! reloaded )
            {
                Singleton::allocate()->taskCommit( task );
                task->stopped = true;
            }
        }
    }

protected:

    virtual void pollingsource_feed_consumers( BurstTask *burst )
    {
        PortInfo *my_pi;
        DataRef ref;
        int selected = 0;
        while( Singleton::allocate()->schedPop(
                    burst, my_pi, ref, &selected ) )
        {
            auto *other_pi( my_pi->other_port );
            //TODO: deal with a kernel depends on multiple producers
            shot_kernel( other_pi->my_kernel, other_pi->my_name, ref, -1 );
        }
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_POLLINGSOURCE_HPP */
