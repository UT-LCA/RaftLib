/**
 * splitmethod.hpp -
 * @author: Jonathan Beard
 * @version: Tue Oct 28 12:56:43 2014
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
#ifndef RAFTSPLITMETHOD_HPP
#define RAFTSPLITMETHOD_HPP  1

#include <type_traits>
#include <functional>

#include "autoreleasebase.hpp"
#include "signalvars.hpp"
#include "port.hpp"
#include "fifo.hpp"


class autoreleasebase;

/** FIXME, it's relativly easy to do zero copy....so implement **/
class splitmethod
{
public:
    splitmethod( Port &port );
    
    virtual ~splitmethod()    = default;

    template < class T /* item */,
               typename std::enable_if<
                         std::is_fundamental< T >::value >::type* = nullptr >
       bool send( T &item, const raft::signal signal = raft::none )
    {
        bool ret_value( true );
        auto &fifo( select_fifo( ret_value ) );
        while( fifo.space_avail() == 0 /** single push **/ )
        {
            fifo = select_fifo( ret_value );
            if( ! ret_value )
            {
                return( ret_value );
            }
        }
        //data is there
        fifo.push( item, signal );
        //we're always returning true
        return( ret_value );
    }

   /**
    * send - this version is intended for the peekrange object from
    * autorelease.tcc in the fifo dir.  I'll add some code to enable
    * only on the autorelease object shortly, but for now this will
    * get it working.
    * @param   range - T&, autorelease object
    * @param   outputs - output port list
    */
   template < class T   /* peek range obj,  */,
              typename std::enable_if<
                       ! std::is_base_of< autoreleasebase,
                                        T >::value >::type* = nullptr >
      bool send( T &range )
   {
        bool ret_value( true );
        auto &fifo( select_fifo( ret_value ) );
        const auto local_range_size( range.size() );
        using index_type = std::remove_const_t< decltype( local_range_size ) >;
        index_type i( 0 );
        while( true )
        {
            const auto space_avail( 
                std::min( fifo.space_avail(), local_range_size ) 
            );
            for( ; i < space_avail; i++ )
            {
               fifo.push( range[ i ].ele, range[ i ].sig );
            }   
            //more data in range, select another FIFO
            if( i < local_range_size )
            {
                fifo = select_fifo( ret_value );
                
            }
            //else no more data, i >= local_range_size, return
            else
            {
                return( true );
            }
        }
        return( true );
   }

   template < class T /* item */ >
      bool get( T &item, raft::signal &signal )
   {
        bool ret_value = true;
        auto &fifo( select_fifo( ret_value ) );
        /**
         * implementing non-blocking spin
         * note: ports will do yield at some
         * point inside kernel if no data 
         * continuously.
         */
        while( fifo.size() == 0 )
        {
            fifo = select_fifo( ret_value );
            if( ! ret_value )
            {
                return( ret_value );
            }
        }
        fifo.pop< T >( item, &signal );
        return( ret_value  );
   }


protected:
    /**
     * select_fifo - this function should return
     * a valid FIFO object based on the implemented 
     * policy. In order to use this effectively, this
     * splitmethod object must be constructed with the
     * correct port container, e.g., "input" or "output"
     * @param  cont - bool value that is set to true or false
     * if the implementing policy wants the caller to continue
     * iterating/calling the select function or exit (when false)
     * to try again later. 
     * @return FIFO - valid port object chosen by implemented
     * selection policy. 
     */
    virtual FIFO&  select_fifo( bool &cont ) = 0;

    /**
     * Note, we can't just keep the iterators since 
     * the iterator bounds could change between 
     * invocations.
     */
    Port &_port;
    //these are initially uninitialized. 
    PortIterator begin;
    PortIterator current;
    PortIterator end;
};
#endif /* END RAFTSPLITMETHOD_HPP */
