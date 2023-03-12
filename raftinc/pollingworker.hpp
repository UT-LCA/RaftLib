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

#include "exceptions.hpp"
#include "defs.hpp"
#include "rafttypes.hpp"
#include "task.hpp"
#include "task_impl.hpp"
#include "allocate/allocate.hpp"
#include "schedule/schedule.hpp"

namespace raft
{

struct ALIGN( L1D_CACHE_LINE_SIZE ) PollingWorker : public TaskImpl
{
    std::unordered_map< port_name_t, std::vector< FIFO* > > fifos_in;
    std::unordered_map< port_name_t, std::vector< FIFO* > > fifos_out;
    std::unordered_map< port_name_t, int > fifos_in_idx;
    std::unordered_map< port_name_t, int > fifos_out_idx;
    std::vector< port_name_t > names_in;
    std::vector< port_name_t > names_out;
    /* the index when there are multiple polling worker clones for a kernel */
    int clone_id;

    kstatus::value_t exe()
    {
        Singleton::schedule()->prepare( this );
        while( ! Singleton::schedule()->shouldExit( this ) )
        {
            if( Singleton::schedule()->readyRun( this ) )
            {
                Singleton::schedule()->precompute( this );
                StreamingData dummy0, dummy1;
                const auto sig_status(
                        (this)->kernel->compute( dummy0, dummy1, this ) );
                Singleton::schedule()->postcompute( this, sig_status );
            }
            Singleton::schedule()->reschedule( this );
        }
        Singleton::schedule()->postexit( this );

        return kstatus::stop;
    }

    virtual void pop( const port_name_t &name, DataRef &item )
    {
        auto *functor(
                (this)->kernel->getInput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_in[ name ] );
        functor->pop( fifos[ fifos_in_idx[ name ] ], item );
        fifos_in_idx[ name ] = ( fifos_in_idx[ name ] + 1 ) % fifos.size();
    }

    virtual DataRef peek( const port_name_t &name )
    {
        auto *functor(
                (this)->kernel->getInput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_in[ name ] );
        return functor->peek( fifos[ fifos_in_idx[ name ] ] );
    }

    virtual void recycle( const port_name_t &name )
    {
        auto *functor(
                (this)->kernel->getInput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_in[ name ] );
        functor->recycle( fifos[ fifos_in_idx[ name ] ] );
        fifos_in_idx[ name ] = ( fifos_in_idx[ name ] + 1 ) % fifos.size();
    }

    virtual void push( const port_name_t &name, DataRef &item )
    {
        auto *functor(
                (this)->kernel->getOutput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_out[ name ] );
        std::cout << id << " push(" << std::hex << (uint64_t)fifos[ 0 ] << std::dec << ")\n";
        functor->push( fifos[ fifos_out_idx [ name ] ], item );
        fifos_out_idx[ name ] = ( fifos_out_idx[ name ] + 1 ) % fifos.size();
    }

    virtual DataRef allocate( const port_name_t &name )
    {
        auto *functor(
                (this)->kernel->getOutput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_out[ name ] );
        return functor->allocate( fifos[ fifos_out_idx[ name ] ] );
    }

    virtual void send( const port_name_t &name )
    {
        auto *functor(
                (this)->kernel->getOutput( name ).runtime_info.fifo_functor );
        auto &fifos( fifos_out[ name ] );
        functor->send( fifos[ fifos_out_idx[ name ] ] );
        fifos_out_idx[ name ] = ( fifos_out_idx[ name ] + 1 ) % fifos.size();
    }

    virtual std::vector< port_name_t > &getNamesIn()
    {
        return names_in;
    }

    virtual std::vector< port_name_t > &getNamesOut()
    {
        return names_out;
    }
};

} /** end namespace raft */
#endif /* END RAFT_POLLINGWORKER_HPP */
