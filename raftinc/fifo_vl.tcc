/**
 * FIFO_VL.cpp -
 * @author: Ashen Ekanayake
 * @version: Thu Aug  9 23:55:45 2020
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
#ifdef VL
#ifndef FIFOVL_TCC
#define FIFOVL_TCC  1

#include <map>
#include "fifoabstract.tcc"
#include "port_info.hpp"
#include "portexception.hpp"
#include "ringbuffertypes.hpp"
#include "vlhandle.hpp"
#include "vl.h"
/** for yield **/
#include "sysschedutil.hpp"

template < class T,  Type::RingBufferType type >
class VLBuffer : public FIFOAbstract < T, type >
{
  public:
    VLBuffer( const std::size_t n_items,
              const std::size_t align,
              void * const data,
              bool is_leader,
              std::size_t group_size) : FIFOAbstract < T, type >()
    {
      std::cout << "Made new fifo " << std::hex << (uint64_t)this << std::dec << std::endl;
      UNUSED(align);
      UNUSED(data);
      (this)->is_leader = is_leader;
      (this)->group_size = group_size;
      (this)->bytes_per_element = ( ext_alloc< T >::value ) ? sizeof( T* ) : sizeof( T );
      /* Set the capacity */
      if( n_items > ( L1D_VL_CACHE_LINE_SIZE / (this)->bytes_per_element )) {
        (this)->cap_max = ( L1D_VL_CACHE_LINE_SIZE / (this)->bytes_per_element );
      } else {
        (this)->cap_max = n_items;
      }
    }

    virtual ~VLBuffer()
    {
      if( !(this)->isProducer ) {
        close_byte_vl_as_consumer( (this)->endpt );
      }
    }

    static FIFO* make_new_fifo( const std::size_t n_items,
                                const std::size_t align,
                                void * const data )
    {
      struct VLBufferInfo *info = (struct VLBufferInfo*) data;
      bool is_leader = info->is_leader;
      std::size_t group_size = info->group_size;
      return( new VLBuffer<T , type>( n_items, align, data,
                                      is_leader, group_size ) );
    }
    virtual std::size_t
    size()
    {
      if ( (this)->isProducer ) {
        return ( vl_producer_size( &((this)->endpt), (this)->bytes_per_element ) );
      } else {
        return ( vl_consumer_size( &((this)->endpt), (this)->bytes_per_element ) );
      }
    }

    virtual std::size_t
    space_avail()
    {
      return ( (this)->capacity() - (this)->size() );
    }

    virtual std::size_t
    capacity()
    {
      return ((this)->cap_max);
    }

    virtual void
    deallocate()
    {
      (this)->producer_data.allocate_called = false;
      vl_deallocate ( &((this)->endpt), (this)->bytes_per_element );
      return;
    }

    virtual void
    send( const raft::signal = raft::none )
    {
      if( !(this)->producer_data.allocate_called ) {
        return;
      }
      (this)->producer_data.allocate_called = false;
      vl_send ( &((this)->endpt), (this)->bytes_per_element );
      return;
    }

    virtual void
    send_range( const raft::signal = raft::none )
    {
      assert(false);
    }

    virtual void
    unpeek(){
      /* Nothing to do */
      return;
    }

    virtual void
    resize( const std::size_t n_items,
                         const std::size_t align,
                         volatile bool &exit_alloc )
    {
      UNUSED( n_items );
      UNUSED( align );
      UNUSED( exit_alloc );
      //TODO
      return;
    }

    virtual float
    get_frac_write_blocked()
    {
      // TODO: Implement this
      return 0.0;
    }

    virtual std::size_t
    get_suggested_count()
    {
      return (this)->cap_max;
    }

    virtual void
    invalidate()
    {
      if( (this)->isProducer ) {
        close_byte_vl_as_producer( (this)->endpt );
        (this)->vlhptr->valid_count.fetch_sub(1, std::memory_order_relaxed);
        if ( (this)->vlhptr->valid_count.load(std::memory_order_relaxed) <= 0 ) {
          (this)->vlhptr->is_valid = false;
        }
      }
      return;
    }

    virtual bool
    is_invalid()
    {
      (this)->is_invalid_cnt >>= 1;
      if ( (this)->is_invalid_cnt == 0 ) {
        (this)->local_is_valid = (this)->vlhptr->is_valid;
        (this)->is_invalid_cnt = 1 << 16;
      }
      if (!(this)->isProducer && !(this)->local_is_valid) {
        int left_in_routing_device = vl_size(&((this)->endpt));
        /* remaining in routing device */
        if (left_in_routing_device > (this)->group_size) {
          (this)->local_is_valid = true;
        } else if (left_in_routing_device > 0 && (this)->is_leader) {
          (this)->local_is_valid = true;
        }
      }
      return ( !(this)->local_is_valid );
    }


  protected:
    bool        local_is_valid    = true;
    std::size_t is_invalid_cnt    = 1 << 16;
    std::size_t bytes_per_element = 0;
    std::size_t cap_max           = 0;
    std::size_t group_size;
    bool        is_leader;

    virtual raft::signal
    signal_peek()
    {
      return raft::none;
    }

    virtual void
    signal_pop()
    {
      (this)->local_pop( nullptr, nullptr );
      return;
    }

    virtual void
    inline_signal_send( const raft::signal sig )
    {
      (this)->local_push( nullptr, sig );
      return;
    }

    virtual void
    local_allocate( void **ptr )
    {
      while(1) {
        if( (this)->space_avail() > 0  ) {
          break;
        }
        raft::yield();
      }
      *ptr = (void*) vl_allocate( &((this)->endpt),
                                  (this)->bytes_per_element);
      (this)->producer_data.allocate_called = true;
      return;
    }

    virtual void
    local_allocate_n( void *ptr, const std::size_t n )
    {
      UNUSED(ptr);
      UNUSED(n);
      assert(false);
    }

    virtual void
    local_push( void *ptr, const raft::signal &signal )
    {
      UNUSED(signal);
      auto &t = (this)->template allocate <T> ();
      t = *( reinterpret_cast< T* >( ptr ) );
      (this)->send(raft::none);
      return;
    }

    virtual void
    local_insert( void *ptr_begin,
                       void *ptr_end,
                       const raft::signal &signal,
                       const std::size_t iterator_type )
    {
      UNUSED(ptr_begin);
      UNUSED(ptr_end);
      UNUSED(signal);
      UNUSED(iterator_type);
      assert(false);
    }

    virtual void
    local_pop( void *ptr, raft::signal *signal )
    {
      UNUSED(signal);
      auto &t = (this)->template peek <T> ();
      *( reinterpret_cast< T* >( ptr ) )= t;
      (this)->recycle();
      return;
    }

    virtual void
    local_pop_range( void *ptr_data,
                          const std::size_t n_items )
    {
      UNUSED(ptr_data);
      UNUSED(n_items);
      assert(false);
    }

    virtual void
    local_peek( void **ptr, raft::signal *signal )
    {
      UNUSED(signal);
      while (1) {
        if( (this)->size() > 0  )
        {
          break;
        } else if( (this)->is_invalid() && ((this)->size() == 0) ) {
          throw ClosedPortAccessException(
              "Accessing closed port with local_peek call, exiting!!" );
        }
        if (NULL != vl_peek ( &((this)->endpt), (this)->bytes_per_element, false)) {
          break;
        } else {
          raft::yield();
        }
        //bool pop_valid = false;
        //byte_vl_pop_non(&((this)->endpt), &pop_valid);
        //if (!pop_valid) {
        //  raft::yield();
        //}
      }
      *ptr = (void*) vl_peek ( &((this)->endpt), (this)->bytes_per_element, true);
      return;
    }


    virtual void
    local_peek_range( void **ptr,
                           void **sig,
                           const std::size_t n_items,
                           std::size_t &curr_pointer_loc )
    {
      UNUSED(ptr);
      UNUSED(sig);
      UNUSED(n_items);
      UNUSED(curr_pointer_loc);
      assert(false);
    }

    virtual void
    local_recycle( std::size_t range )
    {
      UNUSED(range);
      vl_recycle ( &((this)->endpt), (this)->bytes_per_element);
    }

};


#endif /* FIFOVL_TCC */
#endif /* VL */
