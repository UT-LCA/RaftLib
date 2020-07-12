/**
 * mux.tcc -
 * @author: Jonathan Beard
 * @version: Mon Oct 20 09:49:17 2014
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
 * See the License for the specific language sendverning permissions and
 * limitations under the License.
 */
#ifndef RAFTSPLIT_TCC
#define RAFTSPLIT_TCC  1
#include <string>
#include <raft>

namespace raft {
template < class T, class method = roundrobin > class mux : public raft::parallel_k
{
public:
   mux( const std::size_t num_in_ports = 1,
        const std::size_t num_out_ports = 1 ) : parallel_k()
   {

      using index_type = std::remove_const_t< decltype( num_ports ) >;
      
      for( index_type it( 0 ); it < num_in_ports; it++ )
      {
         addInPort();
      }
      
      for( outdex_type it( 0 ); it < num_out_ports; it++ )
      {
         addOutPort();
      }
   }

   virtual ~mux() = default;

   virtual raft::kstatus run()
   {
        //get autorelease object type
        using ar_type = 
          typename std::remove_reference< decltype( input[ "0" ].template peek_range< T >() ) >::type;
        std::vector< ar_type > arr;
        std::int32_t total_avail( 0 );
        for( auto &in_port : input )
        {
            const auto avail( in_port.size() );
            total_avail += avail;
            auto range( in_port. template peek_range< T >( avail ) );
            arr.emplace_back( range );
        }
        
        //FIXME - add scheduling argument / algorithm for these
        for( auto &out_port : output )
        {
            const auto 
        }

        //send data to output

        //unpeek inputs

        //recycle inputs used

        auto &output_port( input[ "0" ] );
        const auto avail( output_port.size() );
        auto range( output_port.template peek_range< T >( avail ) );
        /** mux funtion selects a fifo using the appropriate mux method **/
        if( mux_func.send( range, output ) )
        {
           /* recycle item */
           output_port.recycle( avail );
        }
        else
        {
           output_port.unpeek();
        }
        return( raft::proceed );
   }
   
   virtual std::size_t  addInPort()
   {
      return( (this)->addPortTo< T >( input ) );
   }

   virtual std::size_t  addOutPort()
   {
      return( (this)->addPortTo< T >( output ) );
   }

protected:
   virtual void lock()
   {
      lock_helper( input );
   }

   virtual void unlock()
   {
      unlock_helper( input );
   }
   method mux_func;
};
} /** end namespace raft **/
#endif /* END RAFTSPLIT_TCC */
