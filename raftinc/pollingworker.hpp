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
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_map>

#if UT_FOUND
#include <ut>
#endif

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
    int8_t poll_count;

    PollingWorker() : TaskImpl()
    {
        type = POLLING_WORKER;
        poll_count = 0;
    }

    virtual ~PollingWorker() = default;

    template< class SCHEDULER >
    kstatus::value_t exe()
    {
        StreamingData dummy_in( this, 1 >= kernel->input.size() ?
                                StreamingData::SINGLE_IN :
                                StreamingData::IN );
        StreamingData dummy_out( this, 1 >= kernel->output.size() ?
                                 StreamingData::SINGLE_OUT :
                                 StreamingData::OUT );
        SCHEDULER::prepare( this );
        while( ! SCHEDULER::shouldExit( this ) )
        {
            if( SCHEDULER::readyRun( this ) )
            {
                SCHEDULER::precompute( this );
                const auto sig_status(
                        (this)->kernel->compute( dummy_in, dummy_out ) );
                SCHEDULER::postcompute( this, sig_status );
            }
            SCHEDULER::reschedule( this );
        }
        SCHEDULER::postexit( this );

        return kstatus::stop;
    }
};

struct ALIGN( L1D_CACHE_LINE_SIZE ) CondVarWorker : public PollingWorker
{
#if USE_UT
    rt::Mutex m;
    rt::CondVar cv;
#else
    std::mutex m;
    std::condition_variable cv;
#endif

    CondVarWorker() : PollingWorker()
    {
        type = CONDVAR_WORKER;
    }

    virtual ~CondVarWorker() = default;

    template< class SCHEDULER >
    void wait()
    {
#if USE_UT
        m.Lock();
        while( ! SCHEDULER::readyRun( this ) &&
               ! SCHEDULER::shouldExit( this ) )
        {
            cv.Wait( &m );
        }
        m.Unlock();
#else
        std::unique_lock lk( m );
        cv.wait( lk, [ & ]() {
                return SCHEDULER::readyRun( this ) ||
                       SCHEDULER::shouldExit( this ); } );
        lk.unlock();
#endif
    }

    void wakeup()
    {
#if USE_UT
#if ARMQ_NO_INSTANT_SWAP
        cv.Signal();
#else
        cv.SignalSwap();
#endif
#else
        cv.notify_one();
#endif
    }
};

} /** end namespace raft */
#endif /* END RAFT_POLLINGWORKER_HPP */
