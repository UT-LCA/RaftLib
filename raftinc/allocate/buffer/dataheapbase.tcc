/**
 * dataheapbase.tcc - the base template struct for heap-based data
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Sat Feb 25 09:13:00 2023
 *
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
#ifndef RAFT_ALLOCATE_BUFFER_DATAHEAPBASE_TCC
#define RAFT_ALLOCATE_BUFFER_DATAHEAPBASE_TCC  1
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cinttypes>
#include <iostream>
#include "allocate/buffer/pointer.hpp"
#include "allocate/buffer/signal.hpp"
#include "allocate/buffer/database.tcc"


namespace Buffer
{

/**
 * DataHeapBase - base template for data allocated heap.
 */
template < class T >
struct DataHeapBase : public DataBase< T >
{
   DataHeapBase( T * const ptr,
                 const std::size_t max_cap,
                 const std::size_t start_position ) : DataBase< T >( max_cap )
   {
        assert( ptr != nullptr );
        (this)->store = ptr;
        (this)->external_alloc = true;
        initialize_aux();

        /** set index to be start_position **/
        (this)->signal[ 0 ].index  = start_position;
   }

   DataHeapBase( const std::size_t max_cap ,
                 const std::size_t align = 16 ) : DataBase< T >( max_cap )
   {

#if (defined __linux ) || (defined __APPLE__ )
        int ret_val( 0 );
        ret_val = posix_memalign( (void**)&((this)->store),
                                   align,
                                  (this)->length_store );
        if( ret_val != 0 )
        {
            std::cerr << "posix_memalign returned error (" << ret_val << ")";
            std::cerr << " with message: \n" << strerror( ret_val ) << "\n";
            exit( EXIT_FAILURE );
        }
#elif (defined _WIN64 ) || (defined _WIN32)
        (this)->store =
            reinterpret_cast< T* >(
                    _aligned_malloc( (this)->length_store, align ) );
#else
        /**
         * would use the array allocate, but well...we'd have to
         * figure out how to free it
         */
        (this)->store =
            reinterpret_cast< T* >( malloc( (this)->length_store ) );
#endif
        //FIXME - this should be an exception
        assert( (this)->store != nullptr );
#if (defined __linux ) || (defined __APPLE__ )
        posix_madvise( (this)->store,
                       (this)->length_store,
                       POSIX_MADV_SEQUENTIAL );
#endif
        initialize_aux();
   }

   /**
    * copyFrom - invoke this function when you want to duplicate
    * the FIFO's underlying data structure from one FIFO
    * to the next. You can however no longer use the FIFO
    * "other" unless you are very certain how the implementation
    * works as very bad things might happen.
    * @param other - DataBase< T >*, to be copied
    */
   virtual void copyFrom( DataBase< T > *other )
   {
        if( other->external_alloc )
        {
            //FIXME: throw rafterror that is synchronized
            std::cerr <<
                "FATAL: Attempting to resize a FIFO "
                "that is statically alloc'd\n";
            exit( EXIT_FAILURE );
        }

        new ( &(this)->read_pt  ) Pointer( (other->read_pt),
                                           (this)->max_cap );
        new ( &(this)->write_pt ) Pointer( (other->write_pt),
                                           (this)->max_cap );
        (this)->is_valid = other->is_valid;

        /** buffer is already alloc'd, copy **/
        std::memcpy( (void*)(this)->store /* dst */,
                     (void*)other->store  /* src */,
                     other->length_store );

        /** copy signal buff **/
        std::memcpy( (void*)(this)->signal /* dst */,
                     (void*)other->signal  /* src */,
                     other->length_signal );
        /** stats objects are still valid, copy the ptrs over **/

        (this)->read_stats  = other->read_stats;
        (this)->write_stats = other->write_stats;
        /** since we might use this as a min size, make persistent **/
        (this)->force_resize = other->force_resize;
        /** everything should be put back together now **/
        (this)->thread_access[ 0 ] = other->thread_access[ 0 ];
        (this)->thread_access[ 1 ] = other->thread_access[ 1 ];
   }

   virtual ~DataHeapBase()
   {
      //FREE USED HERE
      if( ! (this)->external_alloc )
      {
#if (defined _WIN64 ) || (defined _WIN32)
		 _aligned_free( (this)->store );
#else
         free( (this)->store );
#endif
      }
      free( (this)->signal );
   }

protected:

   /** initialize_aux - initialize auxiliary data structures provided
    * by DataBase, such as Signal queue, read/write pointers, and block
    * stats.
    */
   void initialize_aux()
   {
        (this)->signal = (Signal*) calloc( (this)->max_cap,
                                           sizeof( Signal ) );
        if( (this)->signal == nullptr )
        {
            //FIXME - this should be an exception too
            perror( "Failed to allocate signal queue!" );
            exit( EXIT_FAILURE );
        }
        /** allocate read and write pointers **/
        /** TODO, see if there are optimizations to be made with sizing and alignment **/
        new ( &(this)->read_pt ) Pointer( (this)->max_cap );
        new ( &(this)->write_pt ) Pointer( (this)->max_cap );
        new ( &(this)->read_stats ) Blocked();
        new ( &(this)->write_stats ) Blocked();
   }
};

} //end namespace Buffer
#endif /* END RAFT_ALLOCATE_BUFFER_DATAHEAPBASE_TCC */
