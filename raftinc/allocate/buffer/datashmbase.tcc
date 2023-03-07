/**
 * datashmbase.tcc - base template struct for shm-base data
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Sat Feb 25 09:29:00 2023
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
#ifndef RAFT_ALLOCATE_BUFFER_DATASHMBASE_TCC
#define RAFT_ALLOCATE_BUFFER_DATASHMBASE_TCC  1
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cinttypes>
#include <iostream>
#if defined __APPLE__ || defined __linux
#include <sys/mman.h>
#include <shm>
#endif
#include "allocate/buffer/pointer.hpp"
#include "allocate/buffer/signal.hpp"
#include "allocate/buffer/database.tcc"
#include "allocate/buffer/buffertypes.hpp"
#include "defs.hpp"


namespace Buffer
{

#if defined __APPLE__ || defined __linux

template < class T >
struct DataShmBase : public DataBase< T >
{

   /**
    * Data - Constructor for SHM based ringbuffer.  Allocates store, signal and
    * ptr structures in separate SHM segments.  Could have been a single one but
    * makes reading the ptr arithmatic a bit more difficult.  TODO, reevaluate
    * if performance wise that might be a good idea, also decide how best to
    * align data elements.
    * @param   max_cap, size_t with number of items to allocate queue for
    * @param   shm_key, const std::string key for opening the SHM,
                        must be same for both ends of the queue
    * @param   dir,     Direction enum for letting this queue know
                        which side we're allocating
    * @param   alignment, size_t with alignment NOTE: haven't implemented yet
    */
   DataShmBase( size_t max_cap,
                const std::string shm_key,
                Direction dir,
                const size_t alignment ) : DataBase< T >( max_cap ),
                                           store_key( shm_key + "_store" ),
                                           signal_key( shm_key + "_key" ),
                                           ptr_key( shm_key + "_ptr" ),
                                           dir( dir )
   {
      UNUSED( alignment );
      /** now work through opening SHM **/
      switch( dir )
      {
         case( Direction::Producer ):
         {
            auto alloc_with_error =
            [&]( void **ptr, const size_t length, const char *key )
            {
               try
               {
                  *ptr = shm::init( key, length );
               }catch( bad_shm_alloc &ex )
               {
                  std::cerr <<
                  "Bad SHM allocate for key (" <<
                     key << ") with length (" << length << ")\n";
                  std::cerr << "Message: " << ex.what() << ", exiting.\n";
                  exit( EXIT_FAILURE );
               }
               assert( *ptr != nullptr );
            };
            alloc_with_error( (void**)&(this)->store,
                              (this)->length_store,
                              store_key.c_str() );
            alloc_with_error( (void**)&(this)->signal,
                              (this)->length_signal,
                              signal_key.c_str() );

            new ( &(this)->write_pt ) Pointer( max_cap );
            new ( &(this)->write_stats ) Blocked();


            (this)->cookie.producer = 0x1337;

            while( (this)->cookie.consumer != 0x1337 )
            {
               std::this_thread::yield();
            }
         }
         break;
         case( Direction::Consumer ):
         {
            auto retry_func = [&]( void **ptr, const char *str )
            {
               std::string error_copy;
               std::int32_t timeout( 1000 );
               while( timeout-- )
               {
                  try
                  {
                     *ptr = shm::open( str );
                  }
                  catch( bad_shm_alloc &ex )
                  {
                     //do nothing
                     timeout--;
                     error_copy = ex.what();
                     std::this_thread::yield();
                     continue;
                  }
                  goto SUCCESS;
               }
               /** timeout reached **/
               std::cerr << "Failed to open shared memory for \"" <<
                  str << "\", exiting!!\n";
               std::cerr << "Error message: " << error_copy << "\n";
               exit( EXIT_FAILURE );
               SUCCESS:;
            };
            retry_func( (void**) &(this)->store,  store_key.c_str() );
            retry_func( (void**) &(this)->signal, signal_key.c_str() );

            assert( (this)->store != nullptr );
            assert( (this)->signal != nullptr );

            new ( &(this)->read_pt ) Pointer( max_cap );
            new ( &(this)->read_stats ) Blocked();


            (this)->cookie.consumer = 0x1337;
            while( (this)->cookie.producer != 0x1337 )
            {
               std::this_thread::yield();
            }
         }
         break;
         default:
         {
            //TODO, add signal handler to cleanup
            std::cerr << "Invalid direction, exiting\n";
            exit( EXIT_FAILURE );
         }
      }
      /** should be all set now **/
   }

   virtual ~DataShmBase()
   {
      switch( dir )
      {
         case( Direction::Producer ):
         {
            delete( &(this)->write_pt );
            delete( &(this)->write_stats );
         }
         break;
         case( Direction::Consumer ):
         {
            delete( &(this)->read_pt );
            delete( &(this)->read_stats );
         }
         break;
         default:
            assert( false );
      } /** end switch **/

       //FREE USED HERE
       if( ! (this)->external_alloc )
       {
          /** three segments of SHM to close **/
          shm::close( store_key.c_str(),
                      (void**) &(this)->store,
                      (this)->length_store,
                      false,
                      true );

       }

       shm::close( signal_key.c_str(),
                   (void**) &(this)->signal,
                   (this)->length_signal,
                   false,
                   true );
   }

   virtual void copyFrom( DataShmBase< T > *other )
   {
      UNUSED( other );
      assert( false );
      /** TODO, implement me **/
   }

   struct Cookie
   {
      std::int32_t producer;
      std::int32_t consumer;
   };

   volatile Cookie         cookie;

   /** process local key copies **/
   const std::string store_key;
   const std::string signal_key;
   const std::string ptr_key;
   const Direction   dir;
};

#endif /* END defined __APPLE__ || defined __linux */

} //end namespace Buffer
#endif /* END RAFT_ALLOCATE_BUFFER_DATASHMBASE_TCC */
