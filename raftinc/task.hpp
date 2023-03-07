/**
 * task.hpp -
 * @author: Qinzhe Wu
 * @version: Sun Feb 26 15:30:00 2023
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
#ifndef RAFT_TASK_HPP
#define RAFT_TASK_HPP  1

#include "exceptions.hpp"
#include "defs.hpp"
#include "rafttypes.hpp"
#include "streamingdata.hpp"
#include "allocate/allocate.hpp"
#include "schedule/schedule.hpp"

namespace raft
{

class Kernel;
//class Allocate;
//class Schedule;

enum TaskType
{
    PollingWorkerTask,
    OneShotTask
};

struct ALIGN( L1D_CACHE_LINE_SIZE ) Task
{
    Kernel *kernel;
    Allocate *alloc;
    Schedule *sched;
    TaskType type;
    std::size_t id;

    virtual kstatus::value_t exe() = 0;

    bool pop( const port_name_t &portname, bool dryrun )
    {
        if( dryrun )
        {
            return alloc->getDataIn( this, portname );
        }
        else
        {
            return alloc->dataInReady( this, portname );
        }
    }

    bool allocate( const port_name_t &portname, bool dryrun )
    {
        if( dryrun )
        {
            return alloc->getBufOut( this, portname );
        }
        else
        {
            return alloc->bufOutReady( this, portname );
        }
    }

    StreamingData &getDataIn()
    {
        return alloc->getDataIn( this );
    }

    StreamingData &getBufOut()
    {
        return alloc->getBufOut( this );
    }
};

} /** end namespace raft */
#endif /* END RAFT_TASK_HPP */
