/**
 * fifoabstract.tcc - encapsulate some of the common components
 * of a FIFO implementation. Basically wrap FIFO interfaces and
 * the underlying data store (managed by datamanger) into one
 * base class.
 *
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:56:21 2023
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
#ifndef RAFT_ALLOCATE_FIFOABSTRACT_TCC
#define RAFT_ALLOCATE_FIFOABSTRACT_TCC  1
#include "raftinc/allocate/buffer/buffertypes.hpp"
#include "raftinc/allocate/buffer/data.tcc"
#include "raftinc/allocate/buffer/blocked.hpp"
#include "raftinc/allocate/buffer/internaldefs.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/datamanager.tcc"
#include "raftinc/defs.hpp"

namespace raft
{

template < class T, Buffer::Type::value_t type >
class FIFOAbstract : public FIFO
{
public:
    FIFOAbstract() : FIFO(){}


    /**
     * invalidate - used by producer thread to label this
     * queue as invalid.  Could be for many differing reasons,
     * however the bottom line is that once empty, this queue
     * will receive no extra data and the receiver must
     * do something to deal with this type of behavior
     * if more data is requested.
     */
    virtual void invalidate()
    {
        auto * const ptr( (this)->datamanager.get() );
        ptr->is_valid = false;
        return;
    }


    /**
     * is_invalid - called by the consumer thread to check
     * if this queue is in fact valid.  This is typically
     * only called if the queue is empty or if the consumer
     * is asking for more data than is currently available.
     * @return bool - true if invalid
     */
    virtual bool is_invalid()
    {
        auto * const ptr( (this)->datamanager.get() );
        return( ! ptr->is_valid );
    }


protected:


    /**
     * setPtrMap
     */
    virtual void setPtrMap( ptr_map_t * const in )
    {
        assert( in != nullptr );
        (this)->consumer_data.in = in;
    }


    /**
     * setPtrSet
     */
    virtual void setPtrSet( ptr_set_t * const out )
    {
        assert( out != nullptr );
        (this)->producer_data.out = out;
    }


    virtual void setInPeekSet( ptr_set_t * const peekset )
    {
        assert( peekset != nullptr );
        (this)->consumer_data.in_peek = peekset;
    }


    virtual void setOutPeekSet( ptr_set_t * const peekset )
    {
        assert( peekset != nullptr );
        (this)->producer_data.out_peek = peekset;
    }


    inline void init() noexcept
    {
        auto * const buffer( datamanager.get() );
        assert( buffer != nullptr );
        producer_data.write_stats = &buffer->write_stats;
        consumer_data.read_stats  = &buffer->read_stats;
    }

    struct ALIGN( L1D_CACHE_LINE_SIZE ) {
        volatile bool               allocate_called = false;
        Buffer::Blocked::value_type n_allocated     = 1;
        /**
         * these pointers are set by the scheduler which
         * calls the garbage collection function. these
         * two capture the addresses of output pointers
         */
        ptr_set_t                   *out         = nullptr;
        ptr_set_t                   *out_peek    = nullptr;
        /**
         * this is set via init callback on fifo construction
         * this prevents the re-calculating of the address
         * over and over....pointer chasing is very bad
         * for cache performance.
         */
        Buffer::Blocked             *write_stats = nullptr;
    } producer_data;


    struct  ALIGN( L1D_CACHE_LINE_SIZE ) {
        /**
         * these pointers are set by the scheduler which
         * calls the garbage collection function. these
         * two capture the addresses of output pointers
         */
        ptr_map_t                   *in         = nullptr;
        ptr_set_t                   *in_peek    = nullptr;
        Buffer::Blocked             *read_stats = nullptr;
    } consumer_data;

    /**
     * upgraded the *data structure to be a DataManager
     * object to enable easier and more intuitive dynamic
     * lock free buffer resizing and re-alignment.
     */
    DataManager< T, type >       datamanager;

}; /** end FIFOAbstract decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_FIFOABSTRACT_TCC */
