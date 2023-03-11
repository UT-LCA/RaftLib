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

#include "exceptions.hpp"
#include "defs.hpp"
#include "rafttypes.hpp"
#include "task.hpp"
#include "task_impl.hpp"
#include "allocate/allocate.hpp"
#include "schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) OneShotTask : public TaskImpl
{
    StreamingData *stream_in;
    StreamingData *stream_out;

    kstatus::value_t exe()
    {
        Singleton::schedule()->precompute( this );
        const auto sig_status(
                (this)->kernel->compute( *stream_in, *stream_out, this ) );
        Singleton::schedule()->postcompute( this, sig_status );
        Singleton::schedule()->reschedule( this );
        return kstatus::stop;
    }

    virtual void pop( const port_name_t &name, DataRef &item )
    {
        // should have all data satisfied by StreamingData
        std::cerr << "Unlikely should OneShotTask invoke pop(" <<
            name << ", &item)\n";
    }

    virtual DataRef peek( const port_name_t &name )
    {
        // should have all data satisfied by StreamingData
        std::cerr << "Unlikely should OneShotTask invoke peek(" <<
            name << ")\n";
        return DataRef();
    }

    virtual void recycle( const port_name_t &name )
    {
        // do nothing, because we have dedicated the data for this task already
    }

    virtual void push( const port_name_t &name, DataRef &item )
    {
        // FIXME: multiple pushes in one exe() that exhausts buffer?
    }

    virtual DataRef allocate( const port_name_t &name )
    {
        // FIXME: multiple pushes in one exe() that exhausts buffer?
        return DataRef();
    }

    virtual void send( const port_name_t &name )
    {
        // FIXME: multiple pushes in one exe() that exhausts buffer?
    }
};

} /** end namespace raft */
#endif /* END RAFT_ONESHOTTASK_HPP */
