/**
 * armqMini.cpp -
 * @author: Qinzhe Wu
 * @version: Thu Mar 02 19:50:00 2023
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

using obj_t = std::int32_t;

class start : public raft::Kernel
{
public:
    start() : raft::Kernel()
    {
        add_output< obj_t >( "y" );
    }

    virtual ~start() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &dataOut,
                                            raft::Task *task )
    {
        dataOut[ "y" ].push< obj_t >( counter, task );
        if( ++counter == 200 )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

    bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "y", dryrun );
    }

private:
    obj_t counter = 0;
};


class last : public raft::Kernel
{
public:
    last() : raft::Kernel()
    {
        add_input< obj_t >( "x" );
    }

    virtual ~last() = default;

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &dataOut,
                                            raft::Task *task )
    {
        obj_t in;
        dataIn[ "x" ].pop< obj_t >( in, task );
        std::cout << in << std::endl;
        if( in != counter++ )
        {
            std::cerr << "failed exit\n";
            exit( EXIT_FAILURE );
        }
        return( raft::kstatus::proceed );
    }

    bool pop( raft::Task *task, bool dryrun )
    {
        return task->pop( "x", dryrun );
    }

    bool allocate( raft::Task *task, bool dryrun )
    {
        return true;
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
