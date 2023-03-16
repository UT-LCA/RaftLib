/**
 * allocateSendPush.cpp - throw an error if internal object
 * pop fails.
 *
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar 08 10:34:26 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2016 Jonathan Beard
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
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <raft>
#include <cstdlib>
#include <cassert>
#include "foodef.tcc"
#include "pipeline.tcc"

static std::vector< std::uintptr_t > A;
static std::vector< std::uintptr_t > B;
static std::vector< std::uintptr_t > C;

using obj_t = foo< 80 >;

class start : public raft::test::start< obj_t >
{
public:
    start() : raft::test::start< obj_t >()
    {
    }

    virtual ~start() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
#ifdef STRING_NAMES    
        auto &mem( bufOut[ "y" ].allocate< obj_t >() );
#else
        auto &mem( bufOut[ "y"_port ].allocate< obj_t >() );
#endif
        A.emplace_back( reinterpret_cast< std::uintptr_t >( &mem ) ); 
        for( auto i( 0 ); i < mem.length; i++ )
        {
            mem.pad[ i ] = static_cast< int >( counter );
        }
#ifdef STRING_NAMES        
        bufOut[ "y" ].send();
#else
        bufOut[ "y"_port ].send();
#endif
        counter++;
        if( counter == 200 )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

private:
    std::size_t counter = 0;
};


class middle : public raft::test::middle< obj_t, obj_t >
{
public:
    middle() : raft::test::middle< obj_t, obj_t >()
    {
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
#ifdef STRING_NAMES    
        auto &val( dataIn[ "x" ].peek< obj_t >() );
        B.emplace_back( reinterpret_cast< std::uintptr_t >( &val ) ); 
        bufOut[ "y" ].push( val);
        dataIn[ "x" ].recycle();
#else
        auto &val( dataIn[ "x"_port ].peek< obj_t >() );
        B.emplace_back( reinterpret_cast< std::uintptr_t >( &val ) ); 
        bufOut[ "y"_port ].push( val );
        dataIn[ "x"_port ].recycle();
#endif
        return( raft::kstatus::proceed );
    }
};


class last : public raft::test::last< obj_t >
{
public:
    last() : raft::test::last< obj_t >()
    {
    }

    virtual ~last() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        obj_t mem;
#ifdef STRING_NAMES        
        dataIn[ "x" ].pop( mem );
#else
        dataIn[ "x"_port ].pop( mem );
#endif
        C.emplace_back( reinterpret_cast< std::uintptr_t >( &mem ) ); 
        /** Jan 2016 - otherwise end up with a signed/unsigned compare w/auto **/
        using index_type = std::remove_const_t<decltype(mem.length)>;
        for( index_type i( 0 ); i < mem.length; i++ )
        {
            //will fail if we've messed something up
            if( static_cast<std::size_t>(mem.pad[ i ]) != counter )
            {
                std::cerr << "failed test\n"; 
                exit( EXIT_FAILURE );
            }
        }
        counter++;
        return( raft::kstatus::proceed );
    }

private:
    std::size_t counter = 0;
};

int
main()
{
    start s;
    last l;
    middle m;

    raft::DAG dag;
    dag += s >> m >> l;
    dag.exe< raft::RuntimeFIFO >();
    for( std::size_t  i( 0 ); i < A.size(); i++ )
    {
        if( ( A[ i ] != B[ i ] ) && ( B[ i ] == C[ i ] ) )
        {
            std::cout << "test failed, first, middle should be equal, last should be diff & always the same\n";
            std::cout << std::hex << A[ i ] << " - " << B[ i ] << " - " << C[ i ] << "\n";
            return( EXIT_FAILURE );
        }
    }
    return( EXIT_SUCCESS );
}
