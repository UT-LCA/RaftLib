/**
 * allocatePopInternal.cpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar 08 10:33:26 2023
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
#include "pipeline.tcc"

using obj_t = std::int32_t;

class start : public raft::test::start< obj_t >
{
public:
    start() : raft::test::start< obj_t >()
    {
    }

    virtual ~start() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        auto &mem( bufOut[ "y" ].allocate< obj_t >( task ) );
        mem = counter++;
        bufOut[ "y" ].send( task );
        if( counter == 200 )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

private:
    obj_t counter = 0;
};



class last : public raft::test::last< obj_t >
{
public:
    last() : raft::test::last< obj_t >()
    {
    }

    virtual ~last() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        obj_t in;
        dataIn[ "x" ].pop( in, task );
        if( in != counter++ )
        {
            std::cerr << "failed exit\n";
            exit( EXIT_FAILURE );
        }
        return( raft::kstatus::proceed );
    }

private:
    obj_t counter = 0;
};

int
main()
{
    start s;
    last l;

    raft::DAG dag;
    dag += s >> l;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
