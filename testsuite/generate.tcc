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

#ifndef STRING_NAMES
template < typename T > class generate : public raft::Kernel
{
public:
    generate( std::int64_t count = 1000 ) : raft::Kernel(),
                                          count( count )
    {
        add_output< T >( "number_stream"_port );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return false;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "number_stream"_port, dryrun );
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        bufOut[ "number_stream"_port ].get< T >() = static_cast< T >( (this)->count );
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
        add_output< std::string >( "number_stream"_port );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return false;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "number_stream"_port, dryrun );
    }

    virtual raft::kstatus compute()
    {
        char str[ 8 ];
        str[7]='\0';
        for( auto i( 0 ); i < 7; i++ )
        {
            str[ i ] = (char) distrib( gen );
        }
        bufOut[ "number_stream"_port ].get< std::string >() =
            static_cast< std::string >( str );
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

#else
template < typename T > class generate : public raft::Kernel
{
public:
    generate( std::int64_t count = 1000 ) : raft::Kernel(),
                                          count( count )
    {
        add_output< T >( "number_stream" );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "number_stream", dryrun );
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        bufOut[ "number_stream" ].allocate< T >( task ) =
            static_cast< T >( (this)->count );
        bufOut[ "number_stream" ].send( task );
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
         add_output< std::string >( "number_stream" );
     }

     virtual bool pop( raft::Task *task, bool dryrun )
     {
         return false;
     }

     virtual bool allocate( raft::Task *task, bool dryrun )
     {
         return task->allocate( "number_stream", dryrun );
     }

     virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                             raft::StreamingData &bufOut,
                                             raft::Task *task )
     {
         char str[ 8 ];
         str[7]='\0';
         for( auto i( 0 ); i < 7; i++ )
         {
             str[ i ] = (char) distrib( gen );
         }
         bufOut[ "number_stream" ].allocate< std::string >( task ) =
             static_cast< std::string >( str );
         bufOut[ "number_stream" ].send( task );
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

#endif

} //end namespace test

} //end namespace raft
#endif /* END GENERATE_TCC */
