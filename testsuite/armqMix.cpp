/**
 * armqMix.cpp -
 * @author: Qinzhe Wu
 * @version: Sat Apr 08 15:11:00 2023
 *
 * Copyright 2023 Regents of the University of Texas
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

#include "pipeline.tcc"

using obj_t = std::int32_t;

class start : public raft::test::start< obj_t >
{
public:
    start() : raft::test::start< obj_t >()
    {
    }

    virtual ~start() = default;

    void reset()
    {
        counter = 0;
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        bufOut[ "y"_port ].push< obj_t >( counter );
        if( ++counter == 200 )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

private:
    obj_t counter = 0;
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
        obj_t in;
        dataIn[ "x"_port ].pop< obj_t >( in );
        for( int i( 0 ); 256 > i; ++i )
        {
            for( int j( 0 ); 256 > j; ++j )
            {
                in = 199 - in;
            }
        }
        bufOut[ "y"_port ].push< obj_t >( in );
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

    void reset()
    {
        for( int i( 0 ); 200 > i; ++i )
        {
            checkboard[ i ] = 0;
        }
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        obj_t in;
        dataIn[ "x"_port ].pop< obj_t >( in );
        std::cout << in << std::endl;
        if( 0 != checkboard[ in ] )
        {
            std::cerr << "failed exit\n";
            exit( EXIT_FAILURE );
        }
        checkboard[ in ]++;
        return( raft::kstatus::proceed );
    }

private:
    obj_t checkboard[ 200 ] = { 0 };
};


int
main()
{
    start s;
    middle m;
    last l;

    raft::DAG dag;
    dag += s >> m >> l * 0;
    dag.exe< raft::RuntimeMix >();

    return( EXIT_SUCCESS );
}
