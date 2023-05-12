/**
 * ringbufferlinked.tcc - a special ringbuffer implementation
 * that uses linked list to make it easy extend without copy overhead.
 *
 * @author: Qinzhe Wu
 * @version: Tue Apr 25 16:56:21 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFERLINKED_TCC
#define RAFT_ALLOCATE_RINGBUFFERLINKED_TCC  1
#include "raftinc/exceptions.hpp"
#include "raftinc/allocate/buffer/buffertypes.hpp"
#include "raftinc/allocate/buffer/inline_traits.tcc"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/defs.hpp"

namespace raft
{

template < class T >
class RingBufferLinked : public FIFO
{
    using TPtr = T*;
public:

    /* data store structure */
    template< class Type, class Enable = void > struct Chunk;

    template< class Type >
    struct Chunk< Type, Buffer::inline_alloc_t< Type > >
    {
        using Ptr = Type*;
        Ptr chunk; /* pointing to an array of Type */

        static Ptr allocate( std::size_t chunk_nitems, std::size_t align,
                             Ptr &ref )
        {
            Ptr cpy = ref;
            std::size_t chunk_nbytes( chunk_nitems * sizeof( Type ) +
                                      sizeof( Ptr ) );
#if (defined __linux ) || (defined __APPLE__ )
            int ret_val( 0 );
            ret_val = posix_memalign( (void**)&ref, align, chunk_nbytes );
            if( ret_val != 0 )
            {
                std::cerr << "posix_memalign returned error (" << ret_val << ")";
                std::cerr << " with message: \n" << strerror( ret_val ) << "\n";
                exit( EXIT_FAILURE );
            }
#elif (defined _WIN64 ) || (defined _WIN32)
            ref = reinterpret_cast< Ptr >(
                    _aligned_malloc( chunk_nbytes, align ) );
#else
            /**
             * would use the array allocate, but well...we'd have to
             * figure out how to free it
             */
            ref = reinterpret_cast< Ptr >( malloc( chunk_nbytes ) );
#endif
            //FIXME - this should be an exception
            assert( ref != nullptr );
#if (defined __linux ) || (defined __APPLE__ )
            posix_madvise( ref, chunk_nbytes, POSIX_MADV_SEQUENTIAL );
#endif
            return cpy;
        }

        static Ptr &nextchunk( std::size_t chunk_nitems, Ptr curr )
        {
            auto *next( reinterpret_cast< Ptr* >( &curr[ chunk_nitems ] ) );
            return *next;
        }

        Type &getRef( std::size_t pos )
        {
            return chunk[ pos ];
        }

        Type &copyFromChunk( const std::size_t pos, void *ref )
        {
            new ( ref ) Type( chunk[ pos ] );
            /* NOTE: this assums that copy constructor exists for Type */
            return chunk[ pos ];
        }

        Type &copyToChunk( const std::size_t pos, void *ref )
        {
            auto tmp( reinterpret_cast< Type* >( ref ) );
            new ( &chunk[ pos ] ) Type( *tmp );
            /* NOTE: this assums that copy constructor exists for Type */
            return chunk[ pos ];
        }

        Type &copyRef( const std::size_t pos, void *ptr )
        {
            auto tmp( reinterpret_cast< Type* >( ptr ) );
            new ( &chunk[ pos ] ) Type( *tmp );
            /* NOTE: this assuming copy constructor exists for Type */
            return chunk[ pos ];
        }
    };

    template< class Type >
    struct Chunk< Type, Buffer::ext_alloc_t< Type > >
    {
        using Ptr = Type*;
        Ptr *chunk; /* pointing to an array of Ptr */

        static Ptr *allocate( std::size_t chunk_nitems, std::size_t align,
                              Ptr *&ref )
        {
            Ptr *cpy = ref;
            std::size_t chunk_nbytes( chunk_nitems * sizeof( Ptr ) +
                                      sizeof( Ptr* ) );
#if (defined __linux ) || (defined __APPLE__ )
            int ret_val( 0 );
            ret_val = posix_memalign( (void**)&ref, align, chunk_nbytes );
            if( ret_val != 0 )
            {
                std::cerr << "posix_memalign returned error (" << ret_val << ")";
                std::cerr << " with message: \n" << strerror( ret_val ) << "\n";
                exit( EXIT_FAILURE );
            }
#elif (defined _WIN64 ) || (defined _WIN32)
            ref = reinterpret_cast< Ptr* >(
                    _aligned_malloc( chunk_nbytes, align ) );
#else
            /**
             * would use the array allocate, but well...we'd have to
             * figure out how to free it
             */
            ref = reinterpret_cast< Ptr* >( malloc( chunk_nbytes ) );
#endif
            //FIXME - this should be an exception
            assert( ref != nullptr );
#if (defined __linux ) || (defined __APPLE__ )
            posix_madvise( ref, chunk_nbytes, POSIX_MADV_SEQUENTIAL );
#endif
            return ref;
        }

        static Ptr* &nextchunk( std::size_t chunk_nitems, Ptr *curr )
        {
            auto *next( reinterpret_cast< Ptr** >( &curr[ chunk_nitems ] ) );
            return *next;
        }

        Ptr &getRef( const std::size_t pos )
        {
            return chunk[ pos ];
        }

        Ptr &copyFromChunk( const std::size_t pos, void *ref )
        {
            auto tmp( reinterpret_cast< Ptr* >( ref ) );
            *tmp = chunk[ pos ];
            return chunk[ pos ];
        }

        Ptr &copyToChunk( const std::size_t pos, void *ref )
        {
            auto tmp( reinterpret_cast< Type* >( ref ) );
            chunk[ pos ] = new Type( *tmp );
            /* NOTE: this assuming copy constructor exists for Type */
            return chunk[ pos ];
        }
    };

    /* bookkeeping meta data structure */
    struct ALIGN( L1D_CACHE_LINE_SIZE ) Decoupled
    {
        Decoupled() = default;
    
        ~Decoupled() = default;
    
        std::size_t ptr; /* my pointer, read pointer or write pointer */
        std::size_t ptr_cp; /* local copy of another pointer */
    
        Chunk< T > chunk_ptr;
    
        uint8_t padding[ L1D_CACHE_LINE_SIZE - sizeof( std::size_t ) * 2 -
                         sizeof( Chunk< T > ) /** padd to cache line **/ ];
    };

    RingBufferLinked( const std::size_t n_items, const std::size_t align ) :
        FIFO(), chunk_nitems( n_items ), chunk_align( align )
    {
        /* the initial two chunks */
        Chunk< T >::allocate( chunk_nitems,
                              chunk_align,
                              prod_global.chunk_ptr.chunk );
        Chunk< T >::allocate( chunk_nitems,
                              chunk_align,
                              cons_global.chunk_ptr.chunk );
        /* form a looped linked list */
        Chunk< T >::nextchunk( chunk_nitems, prod_global.chunk_ptr.chunk ) =
            cons_global.chunk_ptr.chunk;
        Chunk< T >::nextchunk( chunk_nitems, cons_global.chunk_ptr.chunk ) =
            prod_global.chunk_ptr.chunk;
        /* reset all pointers */
        prod_local.chunk_ptr.chunk = cons_local.chunk_ptr.chunk =
            cons_global.chunk_ptr.chunk;
        prod_global.ptr = prod_global.ptr_cp = 0;
        cons_global.ptr = cons_global.ptr_cp = 0;
        prod_local.ptr = prod_local.ptr_cp = 0;
        cons_local.ptr = cons_local.ptr_cp = 0;
        max_cap = chunk_nitems * 2;
        is_valid = true;
    }

    virtual std::size_t size( bool is_prod = false )
    {
        if( is_prod )
        {
            /* might overestimate */
            return prod_local.ptr - prod_local.ptr_cp;
        }
        else
        {
            /* might underestimate */
            if( cons_local.ptr_cp == cons_local.ptr )
            {
                cons_local.ptr_cp = prod_global.ptr; /* update */
            }
            return cons_local.ptr_cp - cons_local.ptr;
        }
    }

    virtual std::size_t space_avail()
    {
        //return max_cap - size( true );
        std::size_t tmp( max_cap - prod_local.ptr + prod_local.ptr_cp );
        /* almost full, the condition to trigger resize */
        if( tmp <= chunk_nitems )
        {
            prod_local.ptr_cp = cons_global.ptr;
            return max_cap - prod_local.ptr + prod_local.ptr_cp;
        }
        return tmp;
    }

    virtual std::size_t capacity()
    {
        return max_cap;
    }

    virtual void deallocate()
    {
        throw MethodNotImplementdException( "RingBufferLinked::deallocate()" );
    }

    virtual void send( const signal::value_t = signal::none )
    {
        advance_producer();
    }

    virtual void send_range( const signal::value_t = signal::none )
    {
        throw MethodNotImplementdException( "RingBufferLinked::send_range()" );
    }

    virtual void unpeek()
    {
        /* nothing to do */
    }

    virtual void get_zero_read_stats( Buffer::Blocked &copy ) override
    {
        copy.bec.count = cons_local.ptr - prod_global.ptr_cp;
        prod_global.ptr_cp = cons_local.ptr;
        /* use prod_global.ptr_cp to memorize last read */
        return;
    }

    virtual void get_zero_write_stats( Buffer::Blocked &copy ) override
    {
        copy.bec.count = prod_local.ptr - cons_global.ptr_cp;
        cons_global.ptr_cp = prod_local.ptr;
        /* use cons_global.ptr_cp to memorize last read */
        return;
    }

    virtual void resize( const std::size_t n_items,
                         const std::size_t align,
                         volatile bool &exit_alloc )
    {
        UNUSED( align );
        UNUSED( exit_alloc );
        while( n_items > max_cap )
        {
            auto &next( Chunk< T >::nextchunk( chunk_nitems,
                                               prod_local.chunk_ptr.chunk ) );
            auto chunk_cpy(
                    Chunk< T >::allocate( chunk_nitems, chunk_align, next ) );
            Chunk< T >::nextchunk( chunk_nitems, next ) = chunk_cpy;
            max_cap += chunk_nitems;
            prod_local.chunk_ptr.chunk = next;
            prod_global.ptr = prod_local.ptr;
        }
    }

    virtual float get_frac_write_blocked()
    {
        return 0.0; /* never blocked */
    }

    virtual std::size_t get_suggested_count()
    {
        return 0;
    }

    virtual void invalidate()
    {
        is_valid = false;
        prod_global.ptr = prod_local.ptr; /* drain uncommitted */
        return;
    }

    virtual bool is_invalid()
    {
        return( ! is_valid );
    }

    static FIFO* make_new_fifo( const std::size_t n_items,
                                const std::size_t align,
                                void * const data )
    {
        UNUSED( data );
        assert( data == nullptr );
        return( new RingBufferLinked< T >( n_items, align ) );
    }

protected:

    virtual signal::value_t signal_peek()
    {
        throw MethodNotImplementdException(
                "RingBufferLinked::signal_peek()" );
        return signal::none;
    }

    virtual void signal_pop()
    {
        throw MethodNotImplementdException( "RingBufferLinked::signal_pop()" );
    }

    virtual void inline_signal_send( const signal::value_t sig )
    {
        UNUSED( sig );
        throw MethodNotImplementdException(
                "RingBufferLinked::inline_signal_send()" );
    }

    virtual void local_allocate( void **ptr )
    {
        *ptr = (void*)&( prod_local.chunk_ptr.getRef(
                    prod_local.ptr % chunk_nitems ) );
    }

    virtual void local_allocate_n( void *ptr, const std::size_t n )
    {
        UNUSED( ptr );
        UNUSED( n );
        throw MethodNotImplementdException(
                "RingBufferLinked::local_allocate_n()" );
    }

    virtual void local_push( void *ptr, const signal::value_t &signal )
    {
        copy_to_chunk( ptr );
        advance_producer();
    }

    virtual void local_insert( void *ptr_begin,
                               void *ptr_end,
                               const signal::value_t &signal,
                               const std::size_t iterator_type )
    {
        //TODO: implement this
        UNUSED( ptr_begin );
        UNUSED( ptr_end );
        UNUSED( signal );
        UNUSED( iterator_type );
        throw MethodNotImplementdException(
                "RingBufferLinked::local_insert()" );
    }

    virtual void local_pop( void *ptr, signal::value_t *signal )
    {
        UNUSED( signal );
        copy_from_chunk( ptr );
        advance_consumer();
    }

    virtual void local_pop_range( void *ptr_data,
                                  const std::size_t n_items )
    {
        //TODO: implement this
        UNUSED( ptr_data );
        UNUSED( n_items );
        throw MethodNotImplementdException(
                "RingBufferLinked::local_pop_range()" );
    }

    virtual void local_peek( void **ptr,
                             signal::value_t *signal )
    {
        *ptr = (void*)&( cons_local.chunk_ptr.getRef(
                    cons_local.ptr % chunk_nitems ) );
        UNUSED( signal );
    }

    virtual void local_peek_range( void **ptr,
                                   void **sig,
                                   const std::size_t n_items,
                                   std::size_t &curr_pointer_loc )
    {
        //TODO: implement this
        UNUSED( ptr );
        UNUSED( sig );
        UNUSED( n_items );
        UNUSED( curr_pointer_loc );
        throw MethodNotImplementdException(
                "RingBufferLinked::local_peek_range()" );
    }

    virtual void local_recycle( std::size_t range )
    {
        advance_consumer();
    }

private:

    inline void advance_consumer()
    {
        cons_local.ptr++;
        if( 0 == ( cons_local.ptr % chunk_nitems ) )
        {
            cons_global.ptr = cons_local.ptr;
            cons_local.chunk_ptr.chunk =
                Chunk< T >::nextchunk( chunk_nitems,
                                       cons_local.chunk_ptr.chunk );
        }
    }

    inline void advance_producer()
    {
        prod_local.ptr++;
        if( 0 == ( prod_local.ptr % chunk_nitems ) )
        {
            /* consumer not at the next chunk, we can safely move to next */
            if( chunk_nitems <= space_avail() )
            {
                prod_local.chunk_ptr.chunk =
                    Chunk< T >::nextchunk( chunk_nitems,
                                           prod_local.chunk_ptr.chunk );
                prod_global.ptr = prod_local.ptr;
                return;
            }
            /* should resize now, allocate and insert a new chunk */
            auto &next( Chunk< T >::nextchunk( chunk_nitems,
                                               prod_local.chunk_ptr.chunk ) );
            auto chunk_cpy(
                    Chunk< T >::allocate( chunk_nitems, chunk_align, next ) );
            Chunk< T >::nextchunk( chunk_nitems, next ) = chunk_cpy;
            max_cap += chunk_nitems;
            prod_local.chunk_ptr.chunk = next;
            prod_global.ptr = prod_local.ptr;
        }
    }

    inline void copy_from_chunk( void *ptr )
    {
        auto read_idx( cons_local.ptr % chunk_nitems );
        cons_local.chunk_ptr.copyFromChunk( read_idx, ptr );
    }

    inline void copy_to_chunk( void *ptr )
    {
        auto write_idx( prod_local.ptr % chunk_nitems );
        prod_local.chunk_ptr.copyToChunk( write_idx, ptr );
    }


    const std::size_t chunk_nitems;
    const std::size_t chunk_align;
    std::size_t max_cap;
    Decoupled prod_local, prod_global;
    Decoupled cons_local, cons_global;
    /* prod_local.ptr >= prod_global.ptr >= cons_local.ptr_cp >=
     * cons_local.ptr >= cons_global.ptr >= prod_local.ptr_cp */
    bool is_valid;

}; /** end RingBufferLinked decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_RINGBUFFERLINKED_TCC */
