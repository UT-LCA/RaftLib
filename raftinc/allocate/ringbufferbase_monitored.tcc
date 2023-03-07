/**
 * ringbufferbase_monitored.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Sat Feb 25 15:46:00 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFERBASE_MONITORED_TCC
#define RAFT_ALLOCATE_RINGBUFFERBASE_MONITORED_TCC 1

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>

#include "buffer/buffertypes.hpp"
#include "allocate/fifoabstract.tcc"
#include "allocate/ringbufferbase.tcc"

//#include "sample.tcc"
//#include "meansampletype.tcc"
//#include "arrivalratesampletype.tcc"
//#include "departureratesampletype.tcc"

namespace raft
{

/**
 * RingBufferBaseMonitored - encapsulates logic for a queue with
 * monitoring enabled.
 */
template < class T, Buffer::Type::value_t B >
class RingBufferBaseMonitored : public RingBufferBase< T, B >
{
public:
    RingBufferBaseMonitored( const std::size_t n, const std::size_t align )
        : RingBufferBase< T, B >(), term( false )
    {
        (this)->datamanager.set(
                new Buffer::Data< T, B >( n, align ) );
        (this)->init();
        /** add monitor types immediately after construction **/
        // sample_master.registerSample( new MeanSampleType< T, type >() );
        // sample_master.registerSample( new ArrivalRateSampleType< T, type >()
        // );
        // sample_master.registerSample( new DepartureRateSampleType< T, type >
        // () );
        //(this)->monitor = new std::thread( Sample< T, type >::run,
        //                                   std::ref( *(this)      /** buffer
        //                                   **/ ),
        //                                   std::ref( (this)->term /** term
        //                                   bool **/ ),
        //                                   std::ref( (this)->sample_master )
        //                                   );
    }

    void monitor_off()
    {
        (this)->term = true;
    }

    virtual ~RingBufferBaseMonitored()
    {
        (this)->term = true;
        // monitor->join();
        // delete( monitor );
        // monitor = nullptr;
        delete( (this)->datamanager.get() );
    }

    std::ostream& printQueueData( std::ostream& stream )
    {
        // stream << sample_master.printAllData( '\n' );
        return (stream);
    }

    virtual void resize( const std::size_t size,
                         const std::size_t align,
                         volatile bool& exit_alloc )
    {
        if( (this)->datamanager.is_resizeable() )
        {
            (this)->datamanager.resize(
                new Buffer::Data< T, B >(size, align), exit_alloc );
        }
        /** else, not resizeable..just return **/
        return;
    }

protected:
    // std::thread       *monitor;
    volatile bool term;
    // Sample< T, type >  sample_master;
};

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFERBASE_MONITORED_TCC */
