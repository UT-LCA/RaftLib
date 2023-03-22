/**
 * foreach.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Thu Mar  9 12:12:51 2023
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
#ifndef RAFT_UTILS_FOREACH_TCC
#define RAFT_UTILS_FOREACH_TCC  1
#include <raft>
namespace raft
{
template < class T > class for_each : public Kernel
{
public:
    for_each( T * const ptr,
              const std::size_t nitems,
              const std::size_t nports = 1 ) : Kernel()
    {
        BufferInfo buf_info;
        T *existing_buffer( reinterpret_cast< T* >( ptr ) );
        const std::size_t inc( nitems / nports );
        const std::size_t adder( nitems % nports );

        using index_type = std::remove_const_t< decltype( nports ) >;
        for( index_type index( 0 ); index < nports; index++ )
        {
            auto name( std::to_string( index ) );
#if STRING_NAMES
            add_output< T >( name );
#else
            raft::port_name_t portname( name.c_str(), name.size() );
            add_output< T >( portname );
#endif

            const std::size_t start_index( index * inc );
            buf_info.ptr = reinterpret_cast< void* >(
                    &existing_buffer[ start_index ] );
            buf_info.nitems = inc + ( (nports - 1) == index ? adder : 0 );
            buf_info.start_index = start_index;
#if STRING_NAMES
            get_port( output, name ).setExistingBuffer( buf_info );
#else
            get_port( output, portname.val ).setExistingBuffer( buf_info );
#endif
        }
    }

    virtual raft::kstatus::value_t compute( StreamingData &dataIn,
                                            StreamingData &bufOut ) override
    {
        /* data are already in the FIFO created from the existing buffer */
        return( raft::kstatus::stop );
    }

    virtual bool pop( Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return true;
    }

};
} /** end namespace raft **/
#endif /* END RAFT_UTILS_FOREACH_TCC */
