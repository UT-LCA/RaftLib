/**
 * database.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:55:21 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2015 Jonathan Beard
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
#ifndef RAFTDATABASE_TCC
#define RAFTDATABASE_TCC  1

#include <cstddef>

#include "raftinc/allocate/buffer/blocked.hpp"
#include "raftinc/allocate/buffer/threadaccess.hpp"
#include "raftinc/allocate/buffer/pointer.hpp"
#include "raftinc/allocate/buffer/signal.hpp"

namespace Buffer
{

/**
 * DataBase - not quite the best name since we
 * conjure up a relational database, but it is
 * literally the base for the Data structs.
 */
template < class T > struct DataBase
{
    DataBase( const std::size_t max_cap ) : max_cap ( max_cap ),
                                            length_store( sizeof( T ) * max_cap ),
                                            length_signal( sizeof( T ) * max_cap ),
                                            dynamic_alloc_size( length_store +
                                                                length_signal )
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
        new ( &(this)->read_pt ) Pointer( max_cap );
        new ( &(this)->write_pt ) Pointer( max_cap );
        new ( &(this)->read_stats ) Blocked();
        new ( &(this)->write_stats ) Blocked();
    }

    virtual ~DataBase() = default;

    /**
     * copyFrom - invoke this function when you want to duplicate
     * the FIFO's underlying data structure from one FIFO
     * to the next. You can however no longer use the FIFO
     * "other" unless you are very certain how the implementation
     * works as very bad things might happen.
     * @param   other - struct to be copied
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

    const std::size_t       max_cap;
    /** sizes, might need to define a local type **/
    const std::size_t       length_store;
    const std::size_t       length_signal;

    const std::size_t       dynamic_alloc_size;


    /**
     * read/write pointer. the thread_access is in
     * between as it's well accessed by both just
     * as frequently as the pointers themselves,
     * so we get decent caching behavior out of
     * doing it this way.
     */
    Pointer                 read_pt;
    ThreadAccess            thread_access[ 2 ];
    Pointer                 write_pt;

    T                       *store          = nullptr;
    Signal                  *signal         = nullptr;
    bool                    external_alloc  = false;
    /** variable set by scheduler, used for shutdown **/
    bool                    is_valid        = true;

    /**
     * these keep reference over how many read/writes are
     * blocked. used for dynamic adaptation.
     */
    Blocked                 read_stats;
    Blocked                 write_stats;

    /** need to force resize, this has the count requested **/
    std::size_t             force_resize    = 0;


    using value_type = T;


    const std::size_t       static_alloc_size = sizeof( DataBase< T > );
};

} /** end namespace Buffer **/
#endif /* END RAFTDATABASE_TCC */
