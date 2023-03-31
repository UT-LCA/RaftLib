/**
 * schedule.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:43:28 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2014 Jonathan Beard
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
#ifndef RAFT_SCHEDULE_SCHEDULE_HPP
#define RAFT_SCHEDULE_SCHEDULE_HPP  1
#include <mutex>
#include <atomic>

#if QTHREAD_FOUND
#include <qthread/qthread.hpp>
#endif
#if UT_FOUND
#include <ut>
#endif

#include "raftinc/signalhandler.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/singleton.hpp"

namespace raft {

class Kernel;
class Task;

struct TaskListNode
{
    TaskListNode( Task *the_task = nullptr ) : task( the_task )
    {
        finished = false;
    }
    virtual ~TaskListNode() = default;
    Task * const task;
    TaskListNode * volatile next = nullptr;
    bool finished;
};

class Schedule
{
public:

    Schedule()
    {
        /* set myself as the singleton scheduler */
        Singleton::schedule( this );
        tasks = new TaskListNode(); /* dummy head */
#if USE_UT
        waitgroup_init( &wg );
#elif USE_QTHREAD
        const auto ret_val( qthread_initialize() );
        if( 0 != ret_val )
        {
            std::cerr << "failure to initialize qthreads runtime, exiting\n";
            exit( EXIT_FAILURE );
        }
#endif
    }

    virtual ~Schedule()
    {
#if USE_QTHREAD
        /** kill off the qthread structures **/
        qthread_finalize();
#endif
    }

    virtual void schedule()
    {
    }

    virtual bool doesOneShot() const
    {
        return false;
    }

#if UT_FOUND
    virtual void globalInitialize()
    {
    }

    virtual void perthreadInitialize()
    {
    }
#endif

    /* Task handlers */

    virtual bool shouldExit( Task* task ) = 0;
    virtual bool readyRun( Task* task ) = 0;

    virtual void precompute( Task* task ) = 0;
    virtual void postcompute( Task* task,
                              const kstatus::value_t sig_status ) = 0;

    virtual void reschedule( Task* task ) = 0;

    virtual void prepare( Task* task ) = 0;
    virtual void postexit( Task* task ) = 0;

protected:

    void wait_tasks_finish()
    {
#if USE_UT
        waitgroup_wait( &wg );
#else
        bool keep_going( true );
        while( keep_going )
        {
            while( ! tasks_mutex.try_lock() )
            {
                raft::yield();
            }
            //exit, we have a lock
            keep_going = false;
            TaskListNode *tparent( tasks );
            //loop over each thread and check if done
            while( nullptr != tparent->next )
            {
                if( tparent->next->finished )
                {
                    TaskListNode *ttmp = tparent->next;
                    tparent->next = ttmp->next;
                    delete ttmp;
                }
                else /* a task ! finished */
                {
                    tparent = tparent->next;
                    keep_going = true;
                }
            }
            //if we're here we have a lock and need to unlock
            tasks_mutex.unlock();
            /**
             * NOTE: added to keep from having to unlock these so frequently
             * might need to make the interval adjustable dep. on app
             */
            std::chrono::milliseconds dura( 3 );
            std::this_thread::sleep_for( dura );
        }
#endif
    }

    std::mutex tasks_mutex;
    TaskListNode *tasks; /* the head of tasks linked list */
    std::atomic< std::size_t > task_id = { 1 };
#if UT_FOUND
    waitgroup_t wg;
#endif

}; /** end Schedule decl **/

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_HPP */
