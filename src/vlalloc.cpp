/**
 * vlalloc.cpp - simple allocation, just initializes the FIFO with a
 * fixed size buffer (512 items) with an alignment of 16-bytes.  This
 * can easily be changed by changing the constants below.
 *
 * @author: Ashen Ekanayake
 * @version: Sat Sep 20 19:56:49 2014
 *
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
#ifndef NOVL
#include <chrono>
#include <thread>
#include "kernelkeeper.tcc"
#include "vlalloc.hpp"
#include "graphtools.hpp"
#include "port_info.hpp"
#include "ringbuffertypes.hpp"
#include "vlhandle.hpp"

vlalloc::vlalloc( raft::map &map,
                    volatile bool &exit_alloc) : Allocate( map, exit_alloc )
{
}

vlalloc::~vlalloc()
{
}


void
vlalloc::run()
{
   std::map<std::string, int> fd_map;

   auto assign_func = [&]( PortInfo &a,
                           PortInfo &b,
                           void *data )
   {
      (void) data;
      std::string key = std::to_string(a.my_kernel->leader_id) + "->" +
        std::to_string(b.my_kernel->leader_id);
      if (fd_map.end() == fd_map.find(key)) {
        int fd = mkvl();
        fd_map.emplace(key, fd);
      }
   };

   auto &container( (this)->source_kernels.acquire() );
   GraphTools::BFS( container, assign_func );

   auto alloc_func = [&]( PortInfo &a,
                          PortInfo &b,
                          void *data )
   {
      (void) data ;
      FIFO  *prod_fifo( nullptr );
      FIFO  *cons_fifo( nullptr );
      VLHandle *vlhptr( nullptr );
      auto &func_map( a.const_map[ Type::VirtualLink ] ); 

      std::string key = std::to_string(a.my_kernel->leader_id) + "->" +
        std::to_string(b.my_kernel->leader_id);
      int fd = fd_map.find(key)->second;

      if (a.getFIFO() != nullptr && b.getFIFO() != nullptr) {
        assert(false);
      }
      else if ( a.getFIFO() ) {
        vlhptr = a.vlhptr;
      }
      else if ( b.getFIFO() ) {
        vlhptr = b.vlhptr;
      }
      else {
        vlhptr = new VLHandle;
        vlhptr->vl_fd = fd;
        vlhptr->valid_count.store(0, std::memory_order_relaxed);
      }
      /* If one of them has a VL file descriptor, use it */
      auto test_func( (*func_map)[ false ] );

      if( a.getFIFO() == nullptr) {
        /** if fixed buffer size, use that, else use INITIAL_ALLOC_SIZE **/
        const auto alloc_size( 
            a.fixed_buffer_size != 0 ? a.fixed_buffer_size : INITIAL_ALLOC_SIZE); 
        struct VLBufferInfo info;
        info.is_leader = a.my_kernel->isLeader();
        info.group_size = a.my_kernel->group_size;
        prod_fifo = test_func( alloc_size,
                               ALLOC_ALIGN_WIDTH,
                               (void*)&info );
        prod_fifo->open_vl( vlhptr, true );
        a.vlhptr = vlhptr;
        a.setFIFO( prod_fifo );
        allocated_fifo.insert( prod_fifo );
      }

      if( b.getFIFO() == nullptr) {
        /** if fixed buffer size, use that, else use INITIAL_ALLOC_SIZE **/
        const auto alloc_size( 
            b.fixed_buffer_size != 0 ? b.fixed_buffer_size : INITIAL_ALLOC_SIZE); 
        struct VLBufferInfo info;
        info.is_leader = b.my_kernel->isLeader();
        info.group_size = b.my_kernel->group_size;
        cons_fifo = test_func( alloc_size  /* items */,
                               ALLOC_ALIGN_WIDTH,
                               (void*)&info );
        cons_fifo->open_vl( vlhptr, false );
        b.setFIFO( cons_fifo );
        b.vlhptr = vlhptr;
        allocated_fifo.insert( cons_fifo );
      }

   };

   GraphTools::BFS( container, alloc_func );
   (this)->source_kernels.release();
   (this)->setReady();
   return;
}
#endif
