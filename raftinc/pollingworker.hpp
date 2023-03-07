/**
 * pollingworker.hpp -
 * @author: Qinzhe Wu
 * @version: Wed Mar 01 13:06:00 2023
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
#ifndef RAFT_POLLINGWORKER_HPP
#define RAFT_POLLINGWORKER_HPP  1

#include "exceptions.hpp"
#include "defs.hpp"
#include "rafttypes.hpp"
#include "task.hpp"
#include "allocate/allocate.hpp"
#include "schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) PollingWorker : Task
{
    kstatus::value_t exe()
    {
        while( ! (this)->sched->shouldExit( this ) )
        {
            if ( (this)->sched->readyRun( this ) )
            {
                (this)->sched->precompute( this );
                const auto sig_status(
                        (this)->kernel->compute(
                            (this)->alloc->getDataIn( this ),
                            (this)->alloc->getBufOut( this ) ) );
                (this)->sched->postcompute( this, sig_status );
            }
            (this)->sched->reschedule( this );
        }
        (this)->sched->postexit( this );

        return kstatus::stop;
    }
};

} /** end namespace raft */
#endif /* END RAFT_POLLINGWORKER_HPP */
