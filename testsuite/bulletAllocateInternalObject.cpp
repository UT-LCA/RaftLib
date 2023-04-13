/**
 * bulletAllocateInternalObject.cpp -
 *
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Thu Apr 13 14:55:26 2023
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


using obj_t = foo< 63 >;

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
        counter++;
        bufOut[ "y"_port ].send();
        if( counter == 200 )
        {
            return( raft::kstatus::stop );
        }
        return( raft::kstatus::proceed );
    }

private:
    std::size_t counter = 0;
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
        dataIn[ "x"_port ].pop( mem );

        using index_type = std::remove_const_t<decltype(mem.length)>;
        if( checkboard[ mem.pad[ 0 ] ] )
        {
            exit( EXIT_FAILURE );
        }
        for( index_type i( 0 ); i < mem.length; i++ )
        {
            if( mem.pad[ i ] != mem.pad[ 0 ] )
            {
                exit( EXIT_FAILURE );
            }
        }
        checkboard[ mem.pad[ 0 ] ] = true;
        return( raft::kstatus::proceed );
    }

private:
    bool checkboard[ 200 ] = { 0 };
};

int
main()
{
    start s;
    last l;

    raft::DAG dag;
    dag += s >> l;
    dag.exe< raft::RuntimeNewBurst >();
    return( EXIT_SUCCESS );
}
