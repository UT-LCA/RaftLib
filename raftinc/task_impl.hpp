/**
 * task_impl.hpp - the class with some common methods implemented for all
 * Task derived classes
 * @author: Qinzhe Wu
 * @version: Tue Mar 07 19:54:00 2023
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
#ifndef RAFT_TASK_IMPL_HPP
#define RAFT_TASK_IMPL_HPP  1

#include "raftinc/task.hpp"
#include "raftinc/streamingdata.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) TaskImpl : public Task
{

    virtual bool pop( const port_name_t &portname, bool dryrun )
    {
        if( dryrun )
        {
            return Singleton::allocate()->getDataIn( this, portname );
        }
        else
        {
            return Singleton::allocate()->dataInReady( this, portname );
        }
    }

    virtual bool allocate( const port_name_t &portname, bool dryrun )
    {
        if( dryrun )
        {
            return Singleton::allocate()->getBufOut( this, portname );
        }
        else
        {
            return Singleton::allocate()->bufOutReady( this, portname );
        }
    }

    virtual StreamingData &getDataIn()
    {
        return Singleton::allocate()->getDataIn( this );
    }

    virtual StreamingData &getBufOut()
    {
        return Singleton::allocate()->getBufOut( this );
    }
};

} /** end namespace raft */
#endif /* END RAFT_TASK_IMPL_HPP */
