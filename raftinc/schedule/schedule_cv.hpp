/**
 * schedule_cv.hpp -
 * @author: Qinzhe Wu
 * @version: Thu Mar 28 10:08:00 2023
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
#ifndef RAFT_SCHEDULE_SCHEDULE_CV_HPP
#define RAFT_SCHEDULE_SCHEDULE_CV_HPP  1
#include <mutex>
#include <condition_variable>

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

namespace raft {

/* cannot just inherit from StdThreadSchedMeta because is_source
 * must be initialized before th thread start executing */
struct CVStdSchedMeta : public TaskSchedMeta
{
    CVStdSchedMeta( Task *the_task ) :
        TaskSchedMeta( the_task ), th( [ & ](){
                (this)->task->sched_meta = this;
                (this)->task->exe(); } )
    {
    }

    virtual ~CVStdSchedMeta()
    {
        th.join();
    }

    virtual void wakeup()
    {
        cv.notify_one();
    }

    void wait()
    {
        std::unique_lock lk( m );
        cv.wait( lk, [ & ]() {
                return Singleton::schedule()->readyRun( task ) ||
                       Singleton::schedule()->shouldExit( task ); } );
        lk.unlock();
    }

    std::thread th;
    /* map every task to a kthread */
    std::mutex m;
    std::condition_variable cv;
};

#if QTHREAD_FOUND
//TODO: find how QThread support condition variable
#endif

#if UT_FOUND
struct CVUTSchedMeta : public TaskSchedMeta
{
    CVUTSchedMeta( Task *the_task, waitgroup_t *the_wg ) :
        TaskSchedMeta( the_task ), wg( the_wg )
    {
        task->sched_meta = this;
        rt::Spawn( [ & ](){ task->exe(); } );
    }

    virtual ~CVUTSchedMeta()
    {
    }

    virtual void wakeup()
    {
        cv.Signal();
    }

    void wait()
    {
        m.Lock();
        while( ! Singleton::schedule()->readyRun( task ) &&
               ! Singleton::schedule()->shouldExit( task ) )
        {
            cv.Wait( &m );
        }
        m.Unlock();
    }

    virtual void done()
    {
        waitgroup_done( wg );
    }

    waitgroup_t *wg;
    rt::Mutex m;
    rt::CondVar cv;
};
#endif

#if USE_UT
using CVSchedMeta = struct CVUTSchedMeta;
//#elif USE_QTHREAD
//using OneShotSchedMeta = struct OneShotQTSchedMeta;
#else
using CVSchedMeta = struct CVStdSchedMeta;
#endif


class ScheduleCV : public ScheduleBasic
{
public:
    ScheduleCV( DAG &dag, Allocate *the_alloc ) :
        ScheduleBasic( dag, the_alloc )
    {
    }
    virtual ~ScheduleCV() = default;

    virtual void prepare( Task *task )
    {
        Singleton::allocate()->registerConsumer( task );
    }

    virtual void reschedule( Task *task )
    {
        auto *t( static_cast< CVSchedMeta* >( task->sched_meta ) );
        t->wait();
    }

protected:

    virtual void make_new_sched_meta( Task *task )
    {
#if USE_UT
        auto *tmeta( new CVSchedMeta( task, &wg ) );
        UNUSED( tmeta );
#else
        auto *tmeta( new CVSchedMeta( task ) );
        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();
#endif
    }

};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_CV_HPP */
