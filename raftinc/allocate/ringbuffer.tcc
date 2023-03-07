/**
 * ringbuffer.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:57:21 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFER_TCC
#define RAFT_ALLOCATE_RINGBUFFER_TCC 1

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

#include "allocate/fifo.hpp"
#include "allocate/fifoabstract.tcc"
#include "allocate/ringbufferbase.tcc"
#include "allocate/ringbufferbase_monitored.tcc"
#include "allocate/buffer/buffertypes.hpp"


namespace raft
{

/**
 * RingBuffer - default specializationnn with heap as buffer type.
 * This version has no "monitor" thread, but does have the ability
 * to query queue size which can be quite useful for some
 * monitoring tasks.
 */
template < class T,
           Buffer::Type::value_t B = Buffer::Type::Heap,
           bool monitor = false >
class RingBuffer : public RingBufferBase< T, B >
{
public:
    /**
     * RingBuffer - default constructor, initializes basic
     * data structures.
     */
    RingBuffer( const std::size_t n, const std::size_t align = 16 )
        : RingBufferBase< T, B >()
    {
        assert( n != 0 );
        (this)->datamanager.set( new Buffer::Data< T, B >(n, align) );
        /** set call-backs to structures inside buffer **/
        (this)->init();
    }

    /**
     * RingBuffer - default constructor, initializes basic
     * data structures.
     */
    RingBuffer( void* const ptr,
                const std::size_t length,
                const std::size_t start_position )
        : RingBufferBase< T, B >()
    {
        assert(length != 0);
        T* ptrcast = reinterpret_cast< T* >(ptr);
        (this)->datamanager.set(
            new Buffer::Data< T, B >( ptrcast, length, start_position ) );
        /** set call-backs to structures inside buffer **/
        (this)->init();
    }

    virtual ~RingBuffer()
    {
        /** TODO, might need to get a lock here **/
        /** might have bad behavior if we double delete **/
        delete( (this)->datamanager.get() );
    }

    /**
     * make_new_fifo - builder function to dynamically
     * allocate FIFO's at the time of execution.  The
     * first two parameters are self explanatory.  The
     * data ptr is a data struct that is dependent on the
     * type of FIFO being built.  In there really is no
     * data necessary so it is expacted to be set to nullptr
     * @param   n_items - std::size_t
     * @param   align   - memory alignment
     * @return  FIFO*
     */
    static FIFO* make_new_fifo( const std::size_t n_items,
                                const std::size_t align,
                                void * const data )
    {
        if( data != nullptr )
        {
            return(
                new RingBuffer< T, Buffer::Type::Heap, false >( data,
                                                                n_items,
                                                                align
                                  /** actually start pos, redesign **/ )
            );
        }
        else
        {
            return( new RingBuffer< T, Buffer::Type::Heap, false >(
                        n_items, align ) );
        }
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

};

template <class T>
class RingBuffer< T, Buffer::Type::Heap, true /* monitor */ >
    : public RingBufferBaseMonitored< T, Buffer::Type::Heap >
{
public:
    /**
     * RingBuffer - default constructor, initializes basic
     * data structures.
     */
    RingBuffer( const std::size_t n, const std::size_t align = 16 )
        : RingBufferBaseMonitored< T, Buffer::Type::Heap >( n, align )
    {
        /** nothing really to do **/
    }

    virtual ~RingBuffer() = default;

    static FIFO* make_new_fifo( const std::size_t n_items,
                                const std::size_t align,
                                void * const data )
    {
        UNUSED( data );
        assert( data == nullptr );
        return( new RingBuffer< T, Buffer::Type::Heap, true >(
                    n_items, align ) );
    }
};


/**
 * SharedMemory
 */
template <class T>
class RingBuffer< T, Buffer::Type::SharedMemory, false >
    : public RingBufferBase< T, Buffer::Type::SharedMemory >
{
public:
    RingBuffer( const std::size_t nitems,
                const std::string key,
                const Buffer::Direction dir,
                const std::size_t align = 16 )
                    : RingBufferBase< T, Buffer::Type::SharedMemory >() ,
                      shm_key( key )
    {
        //TODO this needs to be an shm::open
        //TODO once open, needs to be an in-place allocate for
        //the first object to get the open, otherwise..use cookie
        //technique...all sub-keys will follow main key
        (this)->datamanager.set(
            new Buffer::Data< T, Buffer::Type::SharedMemory >( nitems,
                                                               key,
                                                               dir,
                                                               align )
        );
    }

    virtual ~RingBuffer()
    {
        delete( (this)->datamanager.get() );
    }

    struct Data
    {
        const std::string key;
        Buffer::Direction dir;
    };

    /**
     * make_new_fifo - builder function to dynamically
     * allocate FIFO's at the time of execution.  The
     * first two parameters are self explanatory.  The
     * data ptr is a data struct that is dependent on the
     * type of FIFO being built.  In there really is no
     * data necessary so it is expacted to be set to nullptr
     * @param   n_items - std::size_t
     * @param   align   - memory alignment
     * @return  FIFO*
     */
    static FIFO* make_new_fifo( const std::size_t n_items,
                                const std::size_t align,
                                void * const data )
    {
        auto * const data_ptr( reinterpret_cast<Data*>(data) );
        return( new RingBuffer< T,
                                Buffer::Type::SharedMemory,
                                false >( n_items,
                                         data_ptr->key,
                                         data_ptr->dir,
                                         align )
        );
    }

    virtual void resize( const std::size_t size,
                         const std::size_t align,
                         volatile bool& exit_alloc )
    {
        UNUSED( size );
        UNUSED( align );
        UNUSED( exit_alloc );
        assert(false);
        /** TODO, implement me **/
        return;
    }

protected:
    const std::string shm_key;
};

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFER_TCC */
