/**
 * port_info.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 12:49:56 2023
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
#ifndef RAFT_PORT_INFO_HPP
#define RAFT_PORT_INFO_HPP  1
#include <typeinfo>
#include <typeindex>
#include <cstddef>

#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/fifofunctor.hpp"
#include "raftinc/allocate/ringbuffer.tcc"
#include "raftinc/allocate/buffer/buffertypes.hpp"

namespace raft
{

class Kernel;
class Allocate;

struct BufferInfo
{
    BufferInfo() {}
    BufferInfo( void * const ptr_,
                const std::size_t nitems_,
                const std::size_t start_index_ ) :
        ptr( ptr_ ), nitems( nitems_ ), start_index( start_index_ ) {}
    BufferInfo( const BufferInfo &other ) :
        ptr( other.ptr ), nitems( other.nitems ),
        start_index( other.start_index ) {}
    BufferInfo &operator=( const BufferInfo &other )
    {
        (this)->ptr = other.ptr;
        (this)->nitems = other.nitems;
        (this)->start_index = other.start_index;
        return *this;
    }

    void * ptr = nullptr;
    std::size_t nitems = 0;
    std::size_t start_index = 0;
    /* [0:start_index) out of nitems of the buffer have valid data */
};

/* put the type specific port info data structures used
 * by all runtimes here.
 */
struct PortInfo4Runtime
{
    FIFOFunctor *fifo_functor;
    FIFO **fifos;
    int nfifos;
    BufferInfo existing_buffer;

    PortInfo4Runtime()
    {
        fifo_functor = nullptr;
        fifos = nullptr;
    }

    ~PortInfo4Runtime()
    {
        if( nullptr != fifo_functor )
        {
            delete fifo_functor;
            fifo_functor = nullptr;
        }
    }

    template< class T >
    void init()
    {
        fifo_functor = new FIFOFunctorT< T >();
    }
};

struct PortInfo
{
    PortInfo() : type( typeid( ( *this ) ) ) {}

    PortInfo( const std::type_info &the_type ) : type( the_type ) {}

    PortInfo( const PortInfo &other ) :
        type( other.type ),
        my_kernel( other.my_kernel ), my_name( other.my_name ),
        other_kernel( other.other_kernel ), other_name( other.other_name )
    {
    }

    virtual ~PortInfo() = default;

    template< class T >
    void typeSpecificRuntimeInit()
    {
        runtime_info.init< T >();
    }

    void setExistingBuffer( BufferInfo &buf_info )
    {
        runtime_info.existing_buffer = buf_info;
    }

    /**
     * the type of the port.  regardless of if the buffer itself
     * is impplemented or not.
     */
    std::type_index type;

    Kernel *my_kernel = nullptr;
    port_key_t my_name = null_port_value;

    Kernel *other_kernel = nullptr;
    port_key_t other_name = null_port_value;

    const PortInfo *other_port = nullptr;

    PortInfo4Runtime runtime_info;
};

} /** end namespace raft **/
#endif /* END RAFT_PORT_INFO_HPP */
