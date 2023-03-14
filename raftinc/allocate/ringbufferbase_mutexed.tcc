/**
 * ringbufferbase_mutexed.tcc -
 * @author: Qinzhe Wu
 * @version: Mon Mar 13 11:09:00 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFERBASE_MUTEXED_TCC
#define RAFT_ALLOCATE_RINGBUFFERBASE_MUTEXED_TCC 1

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <utility>

#include "buffer/buffertypes.hpp"
#include "allocate/fifoabstract.tcc"
#include "allocate/ringbufferbase.tcc"

namespace raft
{

/**
 * RingBufferBaseMutexed - encapsulates logic for a queue with
 * a mutex protection.
 */
template < class T, Buffer::Type::value_t B >
class RingBufferBaseMutexed : public RingBufferBase< T, B >
{
public:
    RingBufferBaseMutexed( const std::size_t n, const std::size_t align )
        : RingBufferBase< T, B >()
    {
        (this)->datamanager.set(
                new Buffer::Data< T, B >( n, align ) );
        (this)->init();
    }

    virtual ~RingBufferBaseMutexed()
    {
        delete( (this)->datamanager.get() );
    }

    virtual void resize( const std::size_t size,
                         const std::size_t align,
                         volatile bool& exit_alloc )
    {
        if( (this)->datamanager.is_resizeable() )
        {
            (this)->datamanager.resize(
                new Buffer::Data< T, B >(size, align), exit_alloc );
        }
        /** else, not resizeable..just return **/
        return;
    }

protected:

    virtual FIFO *local_consumer_acquire()
    {
        cons_mu.lock();
        return this;
    }

    virtual void local_consumer_release()
    {
        cons_mu.unlock();
    }

    virtual FIFO *local_producer_acquire()
    {
        prod_mu.lock();
        return this;
    }

    virtual void local_producer_release()
    {
        prod_mu.unlock();
    }

    std::mutex cons_mu, prod_mu;
};

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFERBASE_MUTEXED_TCC */
