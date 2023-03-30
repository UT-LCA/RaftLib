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
#include "raftinc/signalhandler.hpp"
#include "raftinc/rafttypes.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/singleton.hpp"
#include <mutex>

namespace raft {

class Kernel;
class Task;

struct TaskSchedMeta
{
    TaskSchedMeta( Task *the_task = nullptr ) : task( the_task ) {}
    virtual ~TaskSchedMeta() = default;
    /* wakeup - the interface for Allocate to wakeup condition-waiting task */
    virtual void wakeup() {}
    /* done - the interface for UT variants to post waitgroup_done */
    virtual void done() {}
    Task * const task;
    bool finished = false;
    TaskSchedMeta * volatile next = nullptr;
};

class Schedule
{
public:

    Schedule()
    {
        /* set myself as the singleton scheduler */
        Singleton::schedule( this );
        tasks = new TaskSchedMeta(); /* dummy head */
    }

    virtual ~Schedule() = default;

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

    std::mutex tasks_mutex;
    TaskSchedMeta *tasks; /* the head of tasks linked list */
    volatile std::size_t task_id = 1;

}; /** end Schedule decl **/

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_HPP */
