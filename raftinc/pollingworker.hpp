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

#include <vector>
#include <unordered_map>

#include "raftinc/exceptions.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/task.hpp"
#include "raftinc/task_impl.hpp"
#include "raftinc/streamingdata.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) PollingWorker : public TaskImpl
{
    /* the index when there are multiple polling worker clones for a kernel */
    int clone_id;

    kstatus::value_t exe()
    {
        StreamingData dummy_in( this, 1 >= kernel->input.size() ?
                                StreamingData::SINGLE_IN :
                                StreamingData::IN );
        StreamingData dummy_out( this, 1 >= kernel->output.size() ?
                                 StreamingData::SINGLE_OUT :
                                 StreamingData::OUT );
        Singleton::schedule()->prepare( this );
        while( ! Singleton::schedule()->shouldExit( this ) )
        {
            if( Singleton::schedule()->readyRun( this ) )
            {
                Singleton::schedule()->precompute( this );
                const auto sig_status(
                        (this)->kernel->compute( dummy_in, dummy_out ) );
                Singleton::schedule()->postcompute( this, sig_status );
            }
            Singleton::schedule()->reschedule( this );
        }
        Singleton::schedule()->postexit( this );

        return kstatus::stop;
    }
};

} /** end namespace raft */
#endif /* END RAFT_POLLINGWORKER_HPP */
