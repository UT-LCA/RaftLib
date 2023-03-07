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

#include "allocate/fifo.hpp"
#include "allocate/fifofunctor.hpp"
#include "allocate/ringbuffer.tcc"
#include "allocate/buffer/buffertypes.hpp"

namespace raft
{

class Kernel;
class Allocate;

/* put the type specific port info data structures used
 * by all runtimes here.
 */
struct PortInfo4Runtime
{
    /* AllocateFIFO */
    FIFOFunctor *fifo_functor;
    FIFO *fifo;
    //using FIFO_constructor =
    //    FIFO* ( std::size_t /** n_items **/,
    //            std::size_t /** alignof */,
    //            void* /** data struct **/ );
    //FIFO_constructor *fifo_const;

    template< class T >
    void init()
    {
        //fifo_const =
        //    RingBuffer< T, Buffer::Type::Heap, false >::make_new_fifo;
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
        other_kernel( other.other_kernel ), other_name( other.other_name ) {}

    virtual ~PortInfo() = default;

    template< class T >
    void typeSpecificRuntimeInit()
    {
        runtime_info.init< T >();
    }

    /**
     * the type of the port.  regardless of if the buffer itself
     * is impplemented or not.
     */
    std::type_index type;

    Kernel *my_kernel = nullptr;
    port_name_t my_name = null_port_value;

    Kernel *other_kernel = nullptr;
    port_name_t other_name = null_port_value;

    const PortInfo *other_port = nullptr;

    Allocate *alloc = nullptr;

    PortInfo4Runtime runtime_info;
};

} /** end namespace raft **/
#endif /* END RAFT_PORT_INFO_HPP */
