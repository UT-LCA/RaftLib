/**
 * print.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:26:39 2023
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
#ifndef RAFT_UTILS_PRINT_TCC
#define RAFT_UTILS_PRINT_TCC  1

#include <functional>
#include <ostream>
#include <iostream>
#include <raft>
#include <cstdlib>

namespace raft{

class printbase
{
protected:
   std::ostream *ofs = nullptr;
};


template< typename T > class printabstract : public raft::Kernel,
                                             public raft::printbase
{
public:
    printabstract( const std::size_t n_input_ports = 1 ) : raft::Kernel(),
                                                           raft::printbase(),
                                                           input_port_count( n_input_ports )
    {
        using index_type = std::remove_const_t< decltype( n_input_ports ) >;
        for( index_type index( 0 ); index < input_port_count; index++ )
        {
            /** add a port for each index var, all named "input_#" **/
#ifdef STRING_NAMES
            add_input< T >( std::to_string( index ) );
#else
            /**
             * if not strings, the addPort function expects a port_key_name_t struct,
             * so, we have to go and add it.
             */
            add_input< T >( raft::port_key_name_t( index, std::to_string( index ) ) );
#endif
        }
        ofs = &(std::cout);
    }

    printabstract( std::ostream &stream,
                   const std::size_t n_input_ports = 1 )  : raft::Kernel(),
                                                            raft::printbase()
    {
        using index_type = std::remove_const_t< decltype( n_input_ports ) >;
        for( index_type index( 0 ); index < n_input_ports; index++ )
        {
            /** add a port for each index var, all named "input_#" **/
#ifdef STRING_NAMES
            add_input< T >( std::to_string( index ) );
#else
            /**
             * if not strings, the addPort function expects a port_key_name_t struct,
             * so, we have to go and add it.
             */
            add_input< T >( raft::port_key_name_t( index, std::to_string( index ) ) );
#endif
        }
        ofs = &stream;
    }

    virtual bool pop( Task *task, bool dryrun )
    {
        bool ans = false;
        for( std::size_t index( 0 ); index < (this)->input_port_count; index++ )
        {
#ifdef STRING_NAMES
            ans |= task->pop( std::to_string( index ), dryrun );
#else
            ans |= task->pop( raft::port_key_name_t( index, std::to_string( index ) ), dryrun );
#endif
        }
        return ans;
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return true;
    }

protected:
    const std::size_t input_port_count    = 1;
};

template< typename T, char delim = '\0' > class print : public printabstract< T >
{
public:
    print( const std::size_t n_input_ports = 1 ) : printabstract< T >( n_input_ports )
    {
    }

    print( std::ostream &stream,
           const std::size_t n_input_ports = 1 ) : printabstract< T >( stream,
                                                                       n_input_ports )
    {
    }

    print( const print &other ) : print( *other.ofs, other.input_port_count )
    {
    }


    /** enable cloning **/
    //CLONE();

    /**
     * compute -
     * @return raft::kstatus::value_t
     */

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task * task )
    {
        for( std::size_t index( 0 ); index < (this)->input_port_count; index++ )
        {
#ifdef STRING_NAMES
            const auto name( std::to_string( index ) );
#else
            const auto name( raft::port_key_name_t( index, std::to_string( index ) ) );
#endif
            const auto &data( dataIn[ name ].peek< T >( task ) );
            *((this)->ofs) << data << delim;
            dataIn[ name ].recycle( task );
        }
        return( raft::kstatus::proceed );
    }
};


template< typename T > class print< T, '\0' > : public printabstract< T >
{
public:
    print( const std::size_t n_input_ports = 1 ) : printabstract< T >( n_input_ports )
    {
    }

    print( std::ostream &stream,
           const std::size_t n_input_ports = 1 ) : printabstract< T >( stream,
                                                                       n_input_ports )
    {
    }

    print( const print &other ) : print( *other.ofs, other.input_port_count )
    {
    }

    //CLONE();


    /**
     * compute -
     * @return raft::kstatus::value_t
     */
    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        for( std::size_t index( 0 ); index < (this)->input_port_count; index++ )
        {
#ifdef STRING_NAMES
            const auto name( std::to_string( index ) );
#else
            const auto name( raft::port_key_name_t( index, std::to_string( index ) ) );
#endif
            const auto &data( dataIn[ name ].peek< T >( task ) );
            *((this)->ofs) << data;
            dataIn[ name ].recycle( task );
        }
        return( raft::kstatus::proceed );
    }
};

} /* end namespace raft */
#endif /* END RAFTPRINT_TCC */
