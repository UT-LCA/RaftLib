/**
 * ringbufferLinked.cpp - test RingBufferLinked.
 *
 * @author: Qinzhe Wu
 * @version: Wed Apr 26 20:37:26 2023
 *
 * Copyright 2023 The Regents of the University of Texas
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
#define ARMQ_DYNAMIC_ALLOC 1

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
static std::vector< std::uintptr_t > D;

#define FOOLEN 80

using obj_t = foo< FOOLEN >;

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
        if( 100 > counter ) /* test allocate */
        {
            auto &mem( bufOut[ "y"_port ].allocate< obj_t >() );
            A.emplace_back( reinterpret_cast< std::uintptr_t >( &mem ) ); 
            for( auto i( 0 ); i < mem.length; i++ )
            {
                mem.pad[ i ] = static_cast< int >( counter );
            }
            bufOut[ "y"_port ].send();
            counter++;
            return( raft::kstatus::proceed );
        }
        obj_t mem;
        A.emplace_back( reinterpret_cast< std::uintptr_t >( &mem ) ); 
        for( auto i( 0 ); i < mem.length; i++ )
        {
            mem.pad[ i ] = static_cast< int >( counter );
        }
        bufOut[ "y"_port ].push( mem );
        if( 200 == ++counter )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

private:
    std::size_t counter = 0;
};


class middle : public raft::test::middle< obj_t, std::size_t >
{
public:
    middle() : raft::test::middle< obj_t, std::size_t >()
    {
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        auto &val( dataIn[ "x"_port ].peek< obj_t >() );
        B.emplace_back( reinterpret_cast< std::uintptr_t >( &val ) ); 
        if( 100 > val.pad[ 0 ] )
        {
            std::size_t acc( accumulate( val ) );
            C.emplace_back( reinterpret_cast< std::uintptr_t >( &acc ) ); 
            bufOut[ "y"_port ].push( acc );
        }
        else
        {
            auto &acc( bufOut[ "y"_port ].allocate< std::size_t >() );
            C.emplace_back( reinterpret_cast< std::uintptr_t >( &acc ) ); 
            acc = accumulate( val );
            bufOut[ "y"_port ].send();
        }
        dataIn[ "x"_port ].recycle();
        return( raft::kstatus::proceed );
    }

    std::size_t accumulate( obj_t &val )
    {
        std::size_t acc = 0;
        for( auto i( 0 ); val.length > i; ++i )
        {
            acc += val.pad[ i ];
            /* cause some delay to make it slower than the producer,
             * so that the input FIFO would resize */
            for( auto j( 0 ); ( val.length * 10 ) > j; ++j )
            {
                __asm__ volatile ( "\
                        nop \n\
                        nop \n\
                        nop \n\
                        nop \n\
                        nop \n\
                        nop \n\
                        nop \n\
                        nop \n\
                        " );
            }
        }
        return acc;
    }
};


class last : public raft::test::last< std::size_t >
{
public:
    last() : raft::test::last< std::size_t >()
    {
    }

    virtual ~last() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        std::size_t val;
        dataIn[ "x"_port ].pop( val );
        D.emplace_back( reinterpret_cast< std::uintptr_t >( &val ) ); 
        if( ( counter * FOOLEN ) != val )
        {
            std::cerr << "failed test\n"; 
            exit( EXIT_FAILURE );
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
    for( std::size_t i( 0 ); i < 100; i++ )
    {
        if( ( A[ i ] != B[ i ] ) && ( C[ i ] == D[ i ] ) )
        {
            std::cout << "test failed, first, middle should be equal, last should be diff & always the same\n";
            std::cout << std::hex << A[ i ] << " - " << B[ i ] << " - " << C[ i ] << " - " << D[ i ] << "\n";
            return( EXIT_FAILURE );
        }
    }
    for( std::size_t i( 100 ); i < A.size(); i++ )
    {
        if( ( A[ i ] == B[ i ] ) && ( C[ i ] == D[ i ] ) )
        {
            std::cout << "test failed, first, middle should not be equal, last should be diff & always the same\n";
            std::cout << std::hex << A[ i ] << " - " << B[ i ] << " - " << C[ i ] << " - " << D[ i ] << "\n";
            return( EXIT_FAILURE );
        }
    }
    return( EXIT_SUCCESS );
}
