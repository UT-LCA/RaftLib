/**
 * fifofunctor.hpp - the typeless interfaces to interact FIFO
 * @author: Qinzhe Wu
 * @version: Fri Mar  3 11:20:00 2023
 *
 * Copyright 2023 Qinzhe Wu
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
#ifndef RAFT_ALLOCATE_FIFOFUNCTOR_HPP
#define RAFT_ALLOCATE_FIFOFUNCTOR_HPP  1

#include "streamingdata.hpp"
#include "allocate/fifo.hpp"
#include "allocate/ringbuffer.tcc"


namespace raft
{

class FIFOFunctor
{

public:

    /**
     * FIFOFunctor - default constructor for base class.
     */
    FIFOFunctor() = default;

    /**
     * ~FIFOFunctor - default destructor
     */
    virtual ~FIFOFunctor() = default;

    virtual FIFO *make_new_fifo( const std::size_t n_items,
                                 const std::size_t align,
                                 void * const data ) = 0;

    virtual FIFO *make_new_fifo_mutexed( const std::size_t n_items,
                                         const std::size_t align,
                                         void * const data ) = 0;

    virtual std::size_t size( FIFO *fifo )
    {
        return fifo->size();
    }

    virtual std::size_t space_avail( FIFO *fifo )
    {
        return fifo->space_avail();
    }

    virtual std::size_t capacity( FIFO *fifo )
    {
        return fifo->capacity();
    }

    virtual DataRef allocate( FIFO *fifo ) = 0;

    virtual DataRef oneshot_allocate() = 0;

    virtual void deallocate( FIFO *fifo )
    {
        fifo->deallocate();
    }

    //virtual FIFO::autorelease< DataRef, FIFO::allocatetype > allocate_s(
    //        FIFO *fifo ) = 0;

    // TODO: find a way to create the functor interface for
    // this non-trivial allocate
    //template < class T,
    //           class ... Args,
    //           typename std::enable_if<
    //               Buffer::inline_class_alloc< T >::value >::type* =
    //               nullptr >
    //T& allocate( Args&&... params )
    //{
    //    void *ptr( nullptr );
    //    /** call blocks till an element is available **/
    //    local_allocate( &ptr );
    //    T * temp( new (ptr) T( std::forward< Args >( params )... ) );
    //    UNUSED( temp );
    //    return( *( reinterpret_cast< T* >( ptr ) ) );
    //}

    //template < class T,
    //           class ... Args,
    //           typename std::enable_if<
    //               Buffer::ext_alloc< T >::value >::type* = nullptr >
    //T& allocate( Args&&... params )
    //{
    //     T **ptr( nullptr );
    //     /** call blocks till an element is available **/
    //     local_allocate( (void**) &ptr );
    //     *ptr = new T( std::forward< Args >( params )... );
    //     return( **ptr );
    //}
    ///**
    // * allocate_s - "auto-release" version of allocate,
    // * where the action of pushing the memory allocated
    // * to the consumer is handled by the returned object
    // * exiting the calling stack frame. There are two functions
    // * here, one that uses the objec constructor type. This
    // * one is for object types for inline. The next is for
    // * external objects, all of which can have the constructor
    // * called with zero arguments.
    // * @return autorelease< T, allocatetype >
    // */
    //template < class T,
    //           class ... Args,
    //           typename std::enable_if<
    //               Buffer::inline_class_alloc< T >::value >::type* = nullptr >
    //auto allocate_s( Args&&... params ) -> autorelease< T, allocatetype >
    //{
    //    void *ptr( nullptr );
    //    /** call blocks till an element is available **/
    //    local_allocate( &ptr );
    //    new (ptr) T( std::forward< Args >( params )... );
    //    return( autorelease< T, allocatetype >(
    //       reinterpret_cast< T* >( ptr ), ( *this ) ) );
    //}

    //template < class T,
    //           class ... Args,
    //           typename std::enable_if<
    //               Buffer::ext_alloc< T >::value >::type* = nullptr >
    //auto allocate_s( Args&&... params ) -> autorelease< T, allocatetype >
    //{
    //    T **ptr( nullptr );
    //    /** call blocks till an element is available **/
    //    local_allocate( (void**) &ptr );
    //    *ptr = new T( std::forward< Args >( params )... );
    //    return( autorelease< T, allocatetype >(
    //       reinterpret_cast< T* >( *ptr ), ( *this ) ) );
    //}


    virtual std::vector< DataRef > allocate_range( FIFO *fifo,
                                                   const std::size_t n ) = 0;

    virtual void send( FIFO *fifo, const signal::value_t sig = signal::none )
    {
        fifo->send( sig );
    }

    virtual void send_range( FIFO *fifo,
                             const signal::value_t sig = signal::none )
    {
        fifo->send_range( sig );
    }

    virtual void push( FIFO *fifo, DataRef item,
                       const signal::value_t signal = signal::none ) = 0;


    ///**
    // * insert - inserts the range from begin to end in the FIFO,
    // * blocks until space is available.  If the range is greater
    // * than the space available it'll simply block and add items
    // * as space becomes available.  There is the implicit assumption
    // * that another thread is consuming the data, so eventually there
    // * will be room.
    // * @param   begin - iterator_type, iterator to begin of range
    // * @param   end   - iterator_type, iterator to end of range
    // * @param   signal - signal::value_t, default raft::none
    // */
    //template< class iterator_type >
    //void insert( iterator_type begin,
    //             iterator_type end,
    //             const signal::value_t signal = signal::none )
    //{
    //    void *begin_ptr( reinterpret_cast< void* >( &begin ) );
    //    void *end_ptr( reinterpret_cast< void* >( &end ) );
    //    local_insert( begin_ptr,
    //                  end_ptr,
    //                  signal,
    //                  typeid( iterator_type ).hash_code() );
    //    return;
    //}

    virtual void pop( FIFO *fifo, DataRef item,
                      signal::value_t *signal = nullptr ) = 0;

    //virtual FIFO::autorelease< DataRef, FIFO::poptype > pop_s(
    //        FIFO *fifo ) = 0;

    virtual void pop_range( FIFO *fifo, FIFO::pop_range_t< DataRef > &items,
                            const std::size_t n_items ) = 0;

    virtual DataRef peek( FIFO *fifo, signal::value_t *signal = nullptr ) = 0;

    //virtual FIFO::autorelease< DataRef, FIFO::peekrange > peek_range(
    //        FIFO *fifo, const std::size_t n ) = 0;

    virtual void unpeek( FIFO *fifo )
    {
        fifo->unpeek();
    }

    virtual void recycle( FIFO *fifo, const std::size_t range = 1 )
    {
        fifo->recycle( range );
    }

    virtual void get_zero_read_stats( FIFO *fifo, Buffer::Blocked &copy )
    {
        fifo->get_zero_read_stats( copy );
    }

    virtual void get_zero_write_stats( FIFO *fifo, Buffer::Blocked &copy )
    {
        fifo->get_zero_write_stats( copy );
    }

    virtual void resize( FIFO *fifo,
                         const std::size_t n_items,
                         const std::size_t align,
                         volatile bool &exit_alloc )
    {
        fifo->resize( n_items, align, exit_alloc );
    }

    virtual float get_frac_write_blocked( FIFO *fifo )
    {
        return fifo->get_frac_write_blocked();
    }

    virtual std::size_t get_suggested_count( FIFO *fifo )
    {
        return fifo->get_suggested_count();
    }

    virtual void invalidate( FIFO *fifo )
    {
        fifo->invalidate();
    }

    virtual void setPtrMap( FIFO *fifo, ptr_map_t * const in )
    {
        fifo->setPtrMap( in );
    }

    virtual void setPtrSet( FIFO *fifo, ptr_set_t * const out )
    {
        fifo->setPtrSet( out );
    }

    virtual void setInPeekSet( FIFO *fifo, ptr_set_t * const peekset )
    {
        fifo->setInPeekSet( peekset );
    }

    virtual void setOutPeekSet( FIFO *fifo, ptr_set_t * const peekset )
    {
        fifo->setOutPeekSet( peekset );
    }

    virtual bool is_invalid( FIFO *fifo )
    {
        return fifo->is_invalid();
    }

}; /** end FIFOFunctor decl **/


template< class T >
class FIFOFunctorT : public FIFOFunctor
{
public:
    FIFOFunctorT() : FIFOFunctor() {}

    virtual ~FIFOFunctorT() = default;

    virtual FIFO *make_new_fifo( const std::size_t n_items,
                                 const std::size_t align,
                                 void * const data )
    {
        return RingBuffer< T,
                           Buffer::Type::Heap,
                           false >::make_new_fifo( n_items, align, data );
    }

    virtual FIFO *make_new_fifo_mutexed( const std::size_t n_items,
                                         const std::size_t align,
                                         void * const data )
    {
        return RingBuffer< T,
                           Buffer::Type::Heap,
                           true >::make_new_fifo( n_items, align, data );
    }

    virtual DataRef allocate( FIFO *fifo )
    {
        DataRef ref;
        ref.set< T >( fifo->allocate< T >() );
        return ref;
    }

    virtual DataRef oneshot_allocate()
    {
        DataRef ref;
        /* TODO: use constructor for class type */
        T *ptr = reinterpret_cast< T* >( malloc( sizeof( T ) ) );
        ref.set< T >( *ptr );
        return ref;
    }

    //TODO: find a way to create a proper functor interface for autorelease
    //virtual FIFO::autorelease< DataRef, FIFO::allocatetype > allocate_s(
    //        FIFO *fifo )
    //{
    //    auto &tmp( fifo->allocate_s< T >() );
    //    tmp.copiedManually();
    //    return ?
    //}

    virtual std::vector< DataRef > allocate_range( FIFO *fifo,
                                                   const std::size_t n )
    {
        std::vector< DataRef > refs( n );
        const auto &vals( fifo->allocate_range< T >( n ) );
        for( std::size_t i( 0 ); n > i; ++i )
        {
            refs[ i ].set< T >( vals[ i ] );
        }
        return refs;
    }

    virtual void push( FIFO *fifo, DataRef item,
                       const signal::value_t signal = signal::none )
    {
        fifo->push< T >( item.get< T >(), signal );
    }

    virtual void pop( FIFO *fifo, DataRef item,
                      signal::value_t *signal = nullptr )
    {
        //assert( nullptr != &item.get< T >() );
        fifo->pop< T >( item.get< T >(), signal );
    }

    //virtual FIFO::autorelease< DataRef, FIFO::poptype > pop_s(
    //        FIFO *fifo ) = 0;

    virtual void pop_range( FIFO *fifo, FIFO::pop_range_t< DataRef > &items,
                            const std::size_t n_items )
    {
        //TODO: find a better way rather than emulating pop_range with pop
        if( n_items > items.size() )
        {
            items.resize( n_items );
        }
        for( std::size_t i( 0 ); n_items > i; ++i )
        {
            fifo->pop< T >( items[ i ].first.get< T >(), &items[ i ].second );
        }
    }

    virtual DataRef peek( FIFO *fifo, signal::value_t *signal = nullptr )
    {
        DataRef ref;
        ref.set< T >( fifo->peek< T >( signal ) );
        return ref;
    }

    //virtual FIFO::autorelease< DataRef, FIFO::peekrange > peek_range(
    //        FIFO *fifo, const std::size_t n ) = 0;

}; /** end FIFOFunctorT decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_FIFO_HPP */
