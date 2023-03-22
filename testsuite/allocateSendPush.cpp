/**
 * allocateSendPush.cpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar 08 16:19:26 2023
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

using obj_t = foo< 100 >;

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
        auto &mem( bufOut[ "y"_port ].allocate< obj_t >() );
        for( auto i( 0 ); i < mem.length; i++ )
        {
            mem.pad[ i ] = static_cast< int >( counter );
        }
        bufOut[ "y"_port ].send();
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

    virtual ~middle() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        auto &mem( dataIn[ "x"_port ].peek< obj_t >() );
        bufOut[ "y"_port ].push( mem );
        dataIn[ "x"_port ].recycle();
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
        auto &mem( dataIn[ "x"_port ].peek< obj_t >() );
        for( auto i( 0 ); i < mem.length; i++ )
        {
            //will fail if we've messed something up
            if( static_cast<std::size_t>( mem.pad[ i ]) != counter )
            {
                std::cerr << "test failed\n";
                exit( EXIT_FAILURE );
            }
        }
        dataIn[ "x"_port ].recycle();
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
    middle m;
    last l;

    raft::DAG dag;
    dag += s >> m >> l;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
