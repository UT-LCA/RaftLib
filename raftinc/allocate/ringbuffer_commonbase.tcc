/**
 * ringbuffer_commonbase.tcc - the shared code in common for
 * different specializations of RingBufferBase.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Sun Feb  26 13:58:00 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFER_COMMONBASE_TCC
#define RAFT_ALLOCATE_RINGBUFFER_COMMONBASE_TCC  1

#include "raftinc/exceptions.hpp"
#include "raftinc/allocate/buffer/inline_traits.tcc"
#include "raftinc/allocate/buffer/buffertypes.hpp"
//#include "raftinc/prefetch.hpp"
#include "raftinc/defs.hpp"
/** for yield **/
#include "raftinc/sysschedutil.hpp"

namespace raft
{

template< class T, Buffer::Type::value_t B, size_t SIZE >
class DataManager;

/**
 * delete_helper - This helper template method would
 * be specialized by whether type T is class type or not, so
 * that class object instance could invoke the destructor
 * properly while non-class type just do nothing.
 */
template < class T, Buffer::Type::value_t B, class Enable = void >
class delete_helper
{
};


template < class T, Buffer::Type::value_t B >
class delete_helper< T, B, Buffer::ext_alloc_t< T > >
{
  public:
    static void del( Buffer::Data< T, B > * const buff_ptr,
                     const std::size_t idx )
    {
        auto * const ptr =
            reinterpret_cast< T* >( ( buff_ptr->store[ idx ] ) );
        delete( ptr );
    }
};

template < class T, Buffer::Type::value_t B >
class delete_helper< T, B, Buffer::inline_class_alloc_t< T > >
{
  public:
    static void del( Buffer::Data< T, B > * const buff_ptr,
                     const std::size_t idx )
    {
        auto * const ptr =
            reinterpret_cast< T* >( &( buff_ptr->store[ idx ] ) );
        ptr->~T();
    }
};

template < class T, Buffer::Type::value_t B >
class delete_helper< T, B, Buffer::inline_nonclass_alloc_t< T > >
{
  public:
    static void del( Buffer::Data< T, B > * const buff_ptr,
                     const std::size_t idx )
    {
    }
};


/**
 * RingBufferCommonBase - This is an intermediate class that
 * wraps a few common methods implementation above FIFOAbstract,
 * in order to avoid code duplication in the following
 * RingBufferBase specializations (Buffer::Type::Heap/SharedMemory
 * + 3 different inline alloc traits).
 */

template < class T, Buffer::Type::value_t B >
class RingBufferCommonBase : public FIFOAbstract< T, B >
{
public:
    /**
     * RingBufferCommonBase - default constructor, initializes basic
     * data structures.
     */
    RingBufferCommonBase() : FIFOAbstract< T, B >(){}

    virtual ~RingBufferCommonBase() = default;

    /**
     * size - as you'd expect it returns the number of
     * items currently in the queue.
     * @return size_t
     */
    virtual std::size_t size() noexcept
    {
       for( ;; )
       {
          (this)->datamanager.enterBuffer( Buffer::size );
          if( (this)->datamanager.notResizing() )
          {
             auto * const buff_ptr( (this)->datamanager.get() );
TOP:
             const auto   wrap_write( Buffer::Pointer::wrapIndicator( buff_ptr->write_pt  ) ),
                          wrap_read(  Buffer::Pointer::wrapIndicator( buff_ptr->read_pt   ) );

             const auto   wpt( Buffer::Pointer::val( buff_ptr->write_pt ) ),
                          rpt( Buffer::Pointer::val( buff_ptr->read_pt  ) );
             if( R_UNLIKELY (wpt == rpt) )
             {
                /** expect most of the time to be full **/
                if( R_LIKELY( wrap_read < wrap_write ) )
                {
                   (this)->datamanager.exitBuffer( Buffer::size );
                   return( buff_ptr->max_cap );
                }
                else if( wrap_read > wrap_write )
                {
                   /**
                    * TODO, this condition is momentary, however there
                    * is a better way to fix this with atomic operations...
                    * or on second thought benchmarking shows the atomic
                    * operations slows the queue down drastically so, perhaps
                    * this is in fact the best of all possible returns (see
                    * Leibniz or Candide for further info).
                    */
                   raft::yield();
                   goto TOP;
                }
                else
                {
                   (this)->datamanager.exitBuffer( Buffer::size );
                   return( 0 );
                }
             }
             else if( rpt < wpt )
             {
                (this)->datamanager.exitBuffer( Buffer::size );
                return( wpt - rpt );
             }
             else if( rpt > wpt )
             {
                (this)->datamanager.exitBuffer( Buffer::size );
                return( buff_ptr->max_cap - rpt + wpt );
             }
             (this)->datamanager.exitBuffer( Buffer::size );
             return( 0 );
          }
          (this)->datamanager.exitBuffer( Buffer::size );
          raft::yield();
       } /** end for **/
       return( 0 ); /** keep some compilers happy **/
    }


    /**
     * space_avail - returns the amount of space currently
     * available in the queue.  This is the amount a user
     * can expect to write without blocking
     * @return  size_t
     */
    virtual std::size_t space_avail()
    {
        return( (this)->datamanager.get()->max_cap - size() );
    }


    /**
     * capacity - returns the capacity of this queue which is
     * set at compile time by the constructor.
     * @return size_t
     */
    virtual std::size_t capacity()
    {
        return( (this)->datamanager.get()->max_cap );
    }


    /**
     * get_zero_read_stats - sets the param variable
     * to the current blocked stats and then sets the
     * current vars to zero.
     * @param   copy - Blocked&
     */
    virtual void get_zero_read_stats( Buffer::Blocked &copy )
    {

        auto &buff_ptr_stats( (this)->datamanager.get()->read_stats.all );

        copy.all       = buff_ptr_stats;;
        buff_ptr_stats = 0;
    }


    /**
     * get_zero_write_stats - sets the write variable
     * to the current blocked stats and then sets the
     * current vars to zero.
     * @param   copy - Blocked&
     */
    virtual void get_zero_write_stats( Buffer::Blocked &copy )
    {
        auto &buff_ptr_stats( (this)->datamanager.get()->write_stats.all );
        copy.all       = buff_ptr_stats;
        buff_ptr_stats = 0;
    }


    /**
     * get_frac_write_blocked - returns the fraction
     * of time that this queue was blocked.  This might
     * become a private function accessible only to the
     * dynamic allocator mechanism, but for now its a
     * public function.
     * @return float
     */
    virtual float get_frac_write_blocked()
    {
        auto &wr_stats( (this)->datamanager.get()->write_stats );
        const auto copy( wr_stats );
        wr_stats.all = 0;
        if( copy.bec.blocked == 0 || copy.bec.count == 0 )
        {
            return( 0.0 );
        }
        /** else **/
        return( (float) copy.bec.blocked / (float) copy.bec.count );
    }


    /**
     * suggested size if the user asks for more than
     * is available. if that condition never occurs
     * then this will always return zero. if it does
     * then this value can serve as a minimum size
     */
    virtual std::size_t get_suggested_count()
    {
        return( (this)->datamanager.get()->force_resize );
    }


protected:


    /**
     * signal_peek - return signal at head of
     * queue and nothing else
     * @return raft::signal::value_t
     */
    virtual signal::value_t signal_peek()
    {
        /**
         * NOTE: normally I'd say we need exclusion here too,
         * however, since this is a copy and we want this to
         * be quick since it'll be used quite often in tight
         * loops I think we'll be okay with getting the current
         * pointer to the head of the queue and returning the
         * value.  Logically copying the queue shouldn't effect
         * this value since the elements all remain in their
         * location relative to the start of the queue.
         */
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto read_index( Buffer::Pointer::val( buff_ptr->read_pt ) );
        return( buff_ptr->signal[ read_index ] /* make copy */ );
    }


    /**
     * signal_pop - special function fo rthe scheduler to
     * pop the current signal and associated item.
     */
    virtual void signal_pop()
    {
        (this)->local_pop( nullptr, nullptr );
    }

    virtual void inline_signal_send( const signal::value_t sig )
    {
        (this)->local_push( nullptr, sig );
    }


    /**
     * local_insert_helper - Unlike the virtual methods above,
     * this is not to overload FIFO methods, but provide a
     * helper template method for different Heap-based RingBuffer
     * specializations to use.
     */
    template < class iterator_type >
    void local_insert_helper( iterator_type begin,
                              iterator_type end,
                              const signal::value_t &signal )
    {
        auto dist( std::distance( begin, end ) );
        const signal::value_t dummy( signal::none );
        while( dist-- )
        {
            /** use global push function **/
            if( dist == 0 )
            {
                /** NOTE: due to the space-efficient specialization of
                 * vector<bool>, *begin could be a temporary bool value
                 * and we cannot get the address for it
                 */
                (this)->local_push( (void*)&(*begin), signal );
            }
            else
            {
                (this)->local_push( (void*)&(*begin), dummy );
            }
            ++begin;
        }
        return;
    }


    /**
     * insert - inserts the range from begin to end in the queue,
     * blocks until space is available.  If the range is greater than
     * available space on the queue then it'll simply add items as
     * space becomes available.  There is the implicit assumption that
     * another thread is consuming the data, so eventually there will
     * be room.
     * @param   begin - iterator_type, iterator to begin of range
     * @param   end   - iterator_type, iterator to end of range
     */
    virtual void local_insert( void *begin_ptr,
                               void *end_ptr,
                               const signal::value_t &signal,
                               const std::size_t iterator_type )
    {
        using list_iter_t = typename std::list< T >::iterator;
        //using vec_iter_t  = typename std::vector< T >::iterator;
        using func_t      = std::function<
            void ( void*, void*, const signal::value_t& ) >;

        /**
         * FIXME: I suspect this would be faster if the compile time (this)
         * were in fact a pass by reference and the local_insert_helper could be
         * absorbed into the std::function, I think the capture probably
         * has about as bad of perf as std::bind
         */
        const std::map< std::size_t, func_t > func_map
                    = { { typeid( list_iter_t ).hash_code(),
                          [&]( void *b_ptr, void *e_ptr,
                               const signal::value_t &sig )
                          {
                              list_iter_t *begin(
                                      reinterpret_cast< list_iter_t* >(
                                          b_ptr ) );
                              list_iter_t *end(
                                      reinterpret_cast< list_iter_t* >(
                                          e_ptr ) );
                              (this)->local_insert_helper< list_iter_t >(
                                      *begin, *end, sig );
                        //TODO: find a nice way to deal with vector<bool>
                        //  } },
                        //{ typeid( vec_iter_t ).hash_code(),
                        //  [&]( void *b_ptr, void *e_ptr,
                        //       const signal::value_t &sig )
                        //  {
                        //      vec_iter_t *begin(
                        //              reinterpret_cast< vec_iter_t* >(
                        //                  b_ptr ) );
                        //      vec_iter_t *end(
                        //              reinterpret_cast< vec_iter_t* >(
                        //                  e_ptr ) );
                        //      (this)->local_insert_helper< vec_iter_t >(
                        //              *begin, *end, sig );
                          } } };
        auto f( func_map.find( iterator_type ) );
        if( f != func_map.end() )
        {
            (*f).second( begin_ptr, end_ptr, signal );
        }
        else
        {
            /** TODO, throw exception **/
            assert( false );
        }
        return;
    }


    /**
     * pop_range - pops a range and returns it as a std::array.  The
     * exact range to be popped is specified as a template parameter.
     * the static std::array was chosen as its a bit faster, however
     * this might change in future implementations to a std::vector
     * or some other structure.
     */
    virtual void local_pop_range( void *ptr_data,
                                  const std::size_t n_items )
    {
        assert( ptr_data != nullptr );
        if( n_items == 0 )
        {
            return;
        }
        auto *items(
           reinterpret_cast<
              std::vector< std::pair< T, signal::value_t > >* >( ptr_data ) );
        /** just in case **/
        assert( items->size() == n_items );
        /**
         * TODO: same as with the other range function
         * I'm not too  happy with the performance on this
         * one.  It'd be relatively easy to fix with a little
         * time.
         */
        for( auto &pair : (*items))
        {
            (this)->pop( pair.first, &( pair.second ) );
        }
        return;
    }


public:

    virtual void deallocate()
    {
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        delete_helper< T, B >::del( buff_ptr, write_index );
        /**
         * at this point nothing has been allocated
         * externally, and the write pointer hasn't been
         * incremented so just unset the allocate flag
         * and then signal the data manager that we're
         * exiting the buffer
         */
        (this)->producer_data.allocate_called = false;
        (this)->datamanager.exitBuffer( Buffer::allocate );
    }


    /**
     * send- releases the last item allocated by allocate() to
     * the queue.  Function will imply return if allocate wasn't
     * called prior to calling this function.
     * @param signal - const raft::signal signal, default: NONE
     */
    virtual void send( const signal::value_t signal = signal::none )
    {
        if( R_UNLIKELY( ! (this)->producer_data.allocate_called ) )
        {
            return;
        }
        /** should be the end of the write, regardless of which allocate
         * called.
         */
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        buff_ptr->signal[ write_index ] = signal;
        (this)->producer_data.write_stats->bec.count++;
        (this)->producer_data.allocate_called = false;
        Buffer::Pointer::inc( buff_ptr->write_pt );
        (this)->datamanager.exitBuffer( Buffer::allocate );
        (this)->local_producer_release();
    }


    /**
     * send_range - releases the last item allocated by allocate_range() to
     * the queue.  Function will imply return if allocate wasn't
     * called prior to calling this function.
     * @param signal - const raft::signal signal, default: NONE
     */
    virtual void send_range( const signal::value_t signal = signal::none )
    {
        if( ! (this)->producer_data.allocate_called )
        {
            return;
        }
        /** should be the end of the write, regardless of which allocate
         * called.
         */
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        buff_ptr->signal[ write_index ] = signal;
        /* only need to inc one more **/
        auto &n_allocated( (this)->producer_data.n_allocated );
        Buffer::Pointer::incBy( buff_ptr->write_pt, n_allocated );
        (this)->producer_data.write_stats->bec.count += n_allocated;
        (this)->producer_data.allocate_called = false;
        n_allocated = 0;
        (this)->datamanager.exitBuffer( Buffer::allocate_range );
        (this)->local_producer_release();
    }


    virtual void unpeek()
    {
        (this)->datamanager.exitBuffer( Buffer::peek );
    }

protected:


    /**
     * local_allocate - get a reference to an object of type T at the
     * end of the queue.  Should be released to the queue using
     * the push command once the calling thread is done with the
     * memory.
     * @return T&, reference to memory location at head of queue
     */
    virtual void local_allocate( void **ptr )
    {
        (this)->local_producer_acquire();
        for(;;)
        {
            (this)->datamanager.enterBuffer( Buffer::allocate );
            if( (this)->datamanager.notResizing() &&
                (this)->space_avail() > 0 )
            {
                break;
            }
            (this)->datamanager.exitBuffer( Buffer::allocate );
            /** else, set stats,  spin **/
            auto &wr_stats( (this)->producer_data.write_stats->bec.blocked );
            if( wr_stats == 0 )
            {
                wr_stats = 1;
            }
#if __x86_64
            __asm__ volatile("\
              pause"
              :
              :
              : );
#endif
            raft::yield();
        }
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        *ptr = (void*)&( buff_ptr->store[ write_index ] );
        (this)->producer_data.allocate_called = true;
        /** call exitBuffer during push call **/
    }


    virtual void local_allocate_n( void *ptr, const std::size_t n )
    {
        (this)->local_producer_acquire();
        for( ;; )
        {
            (this)->datamanager.enterBuffer( Buffer::allocate_range );
            if( (this)->datamanager.notResizing() &&
                (this)->space_avail() >= n )
            {
                break;
            }
            /**
             * if capacity is in fact too little then:
             * 1) signal exit buffer
             * 2) spin
             * 3) hope the resize thread hits soon
             */
            if( (this)->capacity() < n )
            {
                /** it is tricky to implement resize for inline_class_alloc_t
                 * and ext_alloc_t. */
                //((this)->datamanager.get()->force_resize) = n;
            }
            (this)->datamanager.exitBuffer( Buffer::allocate_range );
            /** else, set stats,  spin **/
            auto &wr_stats( (this)->producer_data.write_stats->bec.blocked );
            if( wr_stats == 0 )
            {
                wr_stats = 1;
            }
#if __x86_64
            __asm__ volatile("\
              pause"
              :
              :
              : );
#endif
            raft::yield();
        }
        auto *container(
           reinterpret_cast< std::vector< std::reference_wrapper< T > >* >(
               ptr ) );
        /**
         * TODO, fix this condition where the user can ask for more,
         * double buffer.
         */
        /** iterate over range, pause if not enough items **/
        auto * const buff_ptr( (this)->datamanager.get() );
        auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        for( ::std::size_t index( 0 ); index < n; index++ )
        {
            /**
             * TODO, fix this logic here, write index must get iterated, but
             * not here
             */
            container->emplace_back( buff_ptr->getRealTRef( write_index ) );
            buff_ptr->signal[ write_index ] = signal::none;
            write_index = ( write_index + 1 ) % buff_ptr->max_cap;
        }
        (this)->producer_data.n_allocated =
            static_cast< decltype( (this)->producer_data.n_allocated ) >( n );
        (this)->producer_data.allocate_called = true;
        /** exitBuffer() called by push_range **/
    }


    /**
     * local_push - implements the pure virtual function from the
     * FIFO interface.  Takes a void ptr as the object which is
     * cast into the correct form and an raft::signal signal. If
     * the ptr is null, then the signal is pushed and the item
     * is junk.  This is an internal method so the only case where
     * ptr should be null is in the case of a system signal being
     * sent.
     * @param   item, void ptr
     * @param   signal, const raft::signal::value_t&
     */
    virtual void local_push( void *ptr, const signal::value_t &signal )
    {
        (this)->local_producer_acquire();
        for(;;)
        {
            (this)->datamanager.enterBuffer( Buffer::push );
            if( (this)->datamanager.notResizing() )
            {
                if( (this)->space_avail() > 0 )
                {
                    break;
                }
            }
            (this)->datamanager.exitBuffer( Buffer::push );
            /** else, set stats,  spin **/
            auto &wr_stats( (this)->producer_data.write_stats->bec.blocked );
            if( wr_stats == 0 )
            {
                wr_stats = 1;
            }
#if __x86_64
            __asm__ volatile("\
              pause"
              :
              :
              : );
#endif
            raft::yield();
        }
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto write_index( Buffer::Pointer::val( buff_ptr->write_pt ) );
        if( ptr != nullptr )
        {
            T *item( reinterpret_cast< T* >( ptr ) );
            /** For ext_alloc_t, there was an optimization by simply copy the
             * pointer over if the push uses a previously peeked buffer.
             * However, it would require template specialization for this
             * method to act differently for inline_alloc_t and ext_alloc_t,
             * even worse the buffer pointer could be have been freed up in
             * fifo_gc(), so I simply comment it away, and always deep copy T.
             */
            //auto **b_ptr( reinterpret_cast< T** >(
            //            &buff_ptr->store[ write_index ] ) );
            //if( (this)->producer_data.out_peek->cend() !=
            //    (this)->producer_data.out_peek->find(
            //            reinterpret_cast< std::uintptr_t >( item ) ) )
            //{
            //    //this was from a previous peek call
            //    (this)->producer_data.out->insert(
            //            reinterpret_cast< std::uintptr_t >( item ) );
            //    *b_ptr = item;
            //}
            //else /** hope we have a move/copy constructor **/
            //{
            //    *b_ptr = new T( *item );
            //}
            buff_ptr->copyRealT( write_index, item );
            (this)->producer_data.write_stats->bec.count++;
        }
        buff_ptr->signal[ write_index ] = signal;
#if defined(__aarch64__)
        asm volatile( "dmb ishst" : : : "memory" );
        /** memory write barrier **/
#endif
        Buffer::Pointer::inc( buff_ptr->write_pt );
#if 0
        if( signal == raft::quit )
        {
           (this)->write_finished = true;
        }
#endif
        (this)->datamanager.exitBuffer( Buffer::push );
        (this)->local_producer_release();
    }


    /**
     * local_pop - read one item from the ring buffer,
     * will block till there is data to be read.  If
     * ptr == nullptr then the item is just thrown away.
     * @return  T, item read.  It is removed from the
     *          q as soon as it is read
     */
    virtual void local_pop( void *ptr, signal::value_t *signal )
    {
        (this)->local_consumer_acquire();
        for(;;)
        {
            (this)->datamanager.enterBuffer( Buffer::pop );
            if( (this)->datamanager.notResizing() )
            {
                if( (this)->size() > 0 )
                {
                    break;
                }
                else if( (this)->is_invalid() && (this)->size() == 0 )
                {
                    throw ClosedPortAccessException(
                       "Accessing closed port with pop call, exiting!!" );
                }
            }
            else
            {
                (this)->datamanager.exitBuffer( Buffer::pop );
                /** handle stats **/
                auto &rd_stats(
                        (this)->consumer_data.read_stats->bec.blocked );
                if( rd_stats == 0 )
                {
                    rd_stats  = 1;
                }
            }
            raft::yield();
        }
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto read_index(
                Buffer::Pointer::val( buff_ptr->read_pt ) );
        if( signal != nullptr )
        {
            *signal = buff_ptr->signal[ read_index ];
        }
        assert( ptr != nullptr );
        /** gotta dereference pointer and copy **/
        T *item( reinterpret_cast< T* >( ptr ) );
        new ( item ) T( buff_ptr->getRealTRef( read_index ) );
        /**
         * we know the object is inline constructed,
         * we should inline destruct it to fix bug
         * #76 -jcb 18Nov2018
         * - applying same fix to out-of-band objects
         */
        delete_helper< T, B >::del( buff_ptr, read_index );
        /** only increment here b/c we're actually reading an item **/
        (this)->consumer_data.read_stats->bec.count++;
        Buffer::Pointer::inc( buff_ptr->read_pt );
        (this)->datamanager.exitBuffer( Buffer::pop );
        (this)->local_consumer_release();
    }


    /**
     * local_peek() - look at a reference to the head of the
     * ring buffer.  This doesn't remove the item, but it
     * does give the user a chance to take a look at it without
     * removing.
     * @return T&
     */
    virtual void local_peek( void **ptr, signal::value_t *signal )
    {
        (this)->local_consumer_acquire();
        for(;;)
        {

            (this)->datamanager.enterBuffer( Buffer::peek );
            if( (this)->datamanager.notResizing() )
            {
                if( (this)->size() > 0 )
                {
                    break;
                }
                else if( (this)->is_invalid() && (this)->size() == 0 )
                {
                    throw ClosedPortAccessException(
                            "Accessing closed port with local_peek call, "
                            "exiting!!" );
                }
            }
            (this)->datamanager.exitBuffer( Buffer::peek );
#if __x86_64
            __asm__ volatile("\
              pause"
              :
              :
              : );
#endif
            raft::yield();
        }
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto read_index( Buffer::Pointer::val( buff_ptr->read_pt ) );
        if( signal != nullptr )
        {
            *signal = buff_ptr->signal[ read_index ];
        }
        *ptr = reinterpret_cast< void* >(
                &( buff_ptr->store[ read_index ] ) );
        /**
         * There was a prefetch optimization for ext_alloc_t
         * as commented below.
         */
        //auto ***real_ptr( reinterpret_cast< T*** >( ptr ) );
        //*real_ptr =
        //    reinterpret_cast< T** >( &( buff_ptr->store[ read_index ] ) );
        ///** prefetch first 1024 bytes **/
        ////probably need to optimize this for each arch / # of threads
        //raft::prefetch< raft::READ,
        //                raft::LOTS,
        //                sizeof( T ) < sizeof( std::uintptr_t ) << 7 ?
        //                sizeof( T ) : sizeof( std::uintptr_t ) << 7
        //                >( **real_ptr );
        //(this)->consumer_data.in_peek->insert(
        //        reinterpret_cast< ptr_t >( **real_ptr ) );
        return;
        /**
         * exitBuffer() called when recycle is called, can't be sure the
         * reference isn't being used until all outside accesses to it are
         * invalidated from the buffer.
         */
    }

    virtual void local_peek_range( void **ptr,
                                   void **sig,
                                   const std::size_t n,
                                   std::size_t &curr_pointer_loc )
    {
        (this)->local_consumer_acquire();
        for(;;)
        {

            (this)->datamanager.enterBuffer( Buffer::peek );
            if( (this)->datamanager.notResizing() )
            {
                if( (this)->size() >= n )
                {
                    break;
                }
                else if( (this)->is_invalid() && (this)->size() == 0 )
                {
                    throw ClosedPortAccessException(
                       "Accessing closed port with local_peek_range call, "
                       "exiting!!" );
                }
                else if( (this)->is_invalid() && (this)->size() < n )
                {
                    throw NoMoreDataException(
                            "Too few items left on closed port, kernel "
                            "exiting" );
                }
            }
            (this)->datamanager.exitBuffer( Buffer::peek );
#if __x86_64
            __asm__ volatile("\
              pause"
              :
              :
              : );
#endif
            raft::yield();
        }

        /**
         * TODO, fix this condition where the user can ask for more,
         * double buffer.
         */
        /** iterate over range, pause if not enough items **/
        auto * const buff_ptr( (this)->datamanager.get() );
        const auto cpl( Buffer::Pointer::val( buff_ptr->read_pt ) );
        curr_pointer_loc = cpl;
        *sig = reinterpret_cast< void* >( &buff_ptr->signal[ cpl ] );
        *ptr = buff_ptr->store;
        return;
    }


    /**
     * removes range items from the buffer, ignores
     * them without the copy overhead.
     */
    virtual void local_recycle( std::size_t range )
    {
        if( range == 0 )
        {
            /** do nothing **/
            return;
        }
        do{ /** at least one to remove **/
            for( ;; )
            {
               (this)->datamanager.enterBuffer( Buffer::recycle );
               if( (this)->datamanager.notResizing() )
               {
                   if( (this)->size() > 0 )
                   {
                       break;
                   }
                   else if( (this)->is_invalid() && (this)->size() == 0 )
                   {
                       (this)->datamanager.exitBuffer( Buffer::recycle );
                       return;
                   }
               }
               (this)->datamanager.exitBuffer( Buffer::recycle );
               raft::yield();
            }
            auto * const buff_ptr( (this)->datamanager.get() );
            const auto read_index( Buffer::Pointer::val( buff_ptr->read_pt ) );
            delete_helper< T, B >::del( buff_ptr, read_index );
            /**
             * TODO, this whole func can be optimized a bit more
             * using the incBy func of Pointer
             */
            Buffer::Pointer::inc( buff_ptr->read_pt );
            (this)->datamanager.exitBuffer( Buffer::recycle );
        } while( --range > 0 );
        (this)->local_consumer_release();
        return;
    }

}; /** end RingBufferCommonBase decl **/

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFER_COMMONBASE_TCC */
