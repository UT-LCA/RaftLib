/**
 * oneshottask.hpp -
 * @author: Qinzhe Wu
 * @version: Wed Mar 01 13:08:00 2023
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
#ifndef RAFT_ONESHOTTASK_HPP
#define RAFT_ONESHOTTASK_HPP  1

#include "raftinc/exceptions.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/task.hpp"
#include "raftinc/task_impl.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) OneShotTask : public TaskImpl
{
    StreamingData *stream_in;
    StreamingData *stream_out;
    bool is_source;
#if USE_QTHREAD
    int group_id;
#endif

    OneShotTask() : TaskImpl()
    {
        type = ONE_SHOT;
    }

    virtual ~OneShotTask() = default;

    kstatus::value_t exe()
    {
        Singleton::schedule()->precompute( this );
        const auto sig_status(
                (this)->kernel->compute( *stream_in, *stream_out ) );
        Singleton::schedule()->postcompute( this, sig_status );
        Singleton::schedule()->reschedule( this );
        return kstatus::stop;
    }
};

} /** end namespace raft */
#endif /* END RAFT_ONESHOTTASK_HPP */
