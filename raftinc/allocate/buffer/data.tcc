/**
 * data.tcc - Data template and specializations on storage and data type
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:55:21 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2020 Jonathan Beard
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
#ifndef RAFT_ALLOCATE_BUFFER_DATA_TCC
#define RAFT_ALLOCATE_BUFFER_DATA_TCC  1
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <thread>
#include <cinttypes>
#include <iostream>
#include <type_traits>
#if defined __APPLE__ || defined __linux
#include <sys/mman.h>
#include <shm>
#include "datashmbase.tcc"
#endif
#include "rafttypes.hpp"
#include "pointer.hpp"
#include "signal.hpp"
#include "buffertypes.hpp"
#include "database.tcc"
#include "dataheapbase.tcc"
#include "inline_traits.tcc"

#include "defs.hpp"

namespace Buffer
{

template < class T,
           Type::value_t B,
           class Enable = void >
struct Data{
    virtual T& getRealTRef( const std::size_t pos ) const = 0;
    virtual T& putRealTByPtr( const std::size_t pos, T* ptr ) = 0;
};

/** buffer structure for "heap" storage class **/
template < class T >
struct Data< T, Type::Heap, inline_alloc_t< T > > : public DataHeapBase< T >
{

    Data( T * const ptr,
          const std::size_t max_cap,
          const std::size_t start_position ) :
        DataHeapBase< T >( ptr, max_cap, start_position )
    {
    }

    Data( const std::size_t max_cap ,
          const std::size_t align = 16 ) : DataHeapBase< T >( max_cap, align )
    {
    }

    virtual T& getRealTRef( const std::size_t pos ) const
    {
        return (this)->store[ pos ];
    }

    virtual T& copyRealT( const std::size_t pos, T* ptr )
    {
        new ( &(this)->store[ pos ] ) T( *ptr );
    }

}; /** end heap < Line Size **/

/** buffer structure for "heap" storage class > line size **/
template < class T >
struct Data< T, Type::Heap, ext_alloc_t< T > > : public DataHeapBase< T* >
{
    using TPtr = T*;

    Data( TPtr const ptr,
          const std::size_t max_cap,
          const std::size_t start_position ) :
        DataHeapBase< TPtr >( reinterpret_cast< TPtr* >( ptr ),
                              max_cap, start_position )
    {
    }

    Data( const std::size_t max_cap ,
          const std::size_t align = 16 ) :
        DataHeapBase< TPtr >( max_cap, align )
    {
    }

    virtual T& getRealTRef( const std::size_t pos ) const
    {
        return *( (this)->store[ pos ] );
    }

    virtual T& copyRealT( const std::size_t pos, T* ptr )
    {
        (this)->store[ pos ] = new T( *ptr );
    }

}; /** end heap > Line Size **/


#if defined __APPLE__ || defined __linux

template < class T >
struct Data< T, Type::SharedMemory > : public DataShmBase< T >
{
    Data( std::size_t max_cap,
          const std::string shm_key,
          Direction dir,
          const std::size_t alignment ) : DataShmBase< T >( max_cap,
                                                            shm_key,
                                                            dir,
                                                            alignment ) {}
    //using DataShmBase< T >( size_t max_cap,
    //                        const std::string shm_key,
    //                        Direction dir,
    //                        const size_t alignment );
};

#endif

} //end namespace Buffer
#endif /* END RAFT_ALLOCATE_BUFFER_DATA_TCC */
