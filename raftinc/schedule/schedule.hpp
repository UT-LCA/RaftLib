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
#include "signalhandler.hpp"
#include "rafttypes.hpp"
#include "defs.hpp"
#include <affinity>

namespace raft {

class Kernel;
class Task;

class Schedule
{
public:

    Schedule() = default;
    virtual ~Schedule() = default;

    virtual void schedule() = 0;

    /* Task handlers */

    virtual bool shouldExit( Task* task ) = 0;
    virtual bool readyRun( Task* task ) = 0;

    virtual void precompute( Task* task ) = 0;
    virtual void postcompute( Task* task,
                              const kstatus::value_t sig_status ) = 0;

    virtual void reschedule( Task* task ) = 0;

    virtual void postexit( Task* task ) = 0;

}; /** end Schedule decl **/

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_HPP */
