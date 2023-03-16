/**
 * generate.tcc - 
 * @author: Jonathan Beard
 * @version: Fri Dec  4 07:26:02 2015
 * 
 * Copyright 2015 Jonathan Beard
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
#ifndef GENERATE_TCC
#define GENERATE_TCC  1
#include <random>
#include <string>
#include <cstdint>

namespace raft
{

namespace test
{

template < typename T > class generate : public raft::Kernel
{
public:
    generate( std::int64_t count = 1000 ) : raft::Kernel(),
                                          count( count )
    {
#ifndef STRING_NAMES
        add_output< T >( "number_stream"_port );
#else
        add_output< T >( "number_stream" );
#endif
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#ifndef STRING_NAMES
        return task->allocate( "number_stream"_port, dryrun );
#else
        return task->allocate( "number_stream", dryrun );
#endif
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
#ifndef STRING_NAMES
        bufOut[ "number_stream"_port ].allocate< T >() =
            static_cast< T >( (this)->count );
        bufOut[ "number_stream"_port ].send();
#else
        bufOut[ "number_stream" ].allocate< T >() =
            static_cast< T >( (this)->count );
        bufOut[ "number_stream" ].send();
#endif
        if( count-- > 1 )
        {
            return( raft::kstatus::proceed );
        }
        /** else **/
        return( raft::kstatus::stop );
    }

private:
    std::int64_t count;
};


template <> class generate< std::string > : public raft::Kernel
{
public:
    generate( std::int64_t count = 1000 ) : raft::Kernel(),
                                            count( count ),
                                            gen( 15 ),
                                            distrib( 65, 90 )
    {
#ifndef STRING_NAMES
        add_output< std::string >( "number_stream"_port );
#else
        add_output< std::string >( "number_stream" );
#endif
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#ifndef STRING_NAMES
        return task->allocate( "number_stream"_port, dryrun );
#else
        return task->allocate( "number_stream", dryrun );
#endif
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        char str[ 8 ];
        str[7]='\0';
        for( auto i( 0 ); i < 7; i++ )
        {
            str[ i ] = (char) distrib( gen );
        }
#ifndef STRING_NAMES
        bufOut[ "number_stream"_port ].allocate< std::string >() =
            static_cast< std::string >( str );
        bufOut[ "number_stream"_port ].send();
#else
        bufOut[ "number_stream" ].allocate< std::string >() =
            static_cast< std::string >( str );
        bufOut[ "number_stream" ].send();
#endif
        if( count-- > 1 )
        {
            return( raft::kstatus::proceed );
        }
        /** else **/
        return( raft::kstatus::stop );
    }

private:
    std::int64_t count;
    std::mt19937 gen;
    std::uniform_int_distribution<> distrib;
};

} //end namespace test

} //end namespace raft
#endif /* END GENERATE_TCC */
