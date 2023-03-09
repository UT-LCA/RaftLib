/**
 * vectorAlloc.cpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar  8 23:43:14 2023
 *
 * Copyright 2023 The Regents of the University of Texas
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

#include <raft>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace raft;
using type_t = std::vector< std::uint32_t >;
using lambda_kernel = raft::lambdak< type_t >;

/** hacky print function **/
std::ostream& operator << ( std::ostream &s, const type_t &t )
{
    s << "{ ";
    for( const auto &x : t )
    {
        s << x << ", ";
    }
    s << "}\n";
    return( s );
}

/**
 * hacky above b/c I include this here so the print is visible to the
 * include
 */
#include <raftio>

/** lessen typing later **/
using p_out = raft::print< type_t, '\n' >;

int
main()
{
    p_out print;

    auto v_lambda( [&]( raft::StreamingData &dataIn,
                        raft::StreamingData &bufOut,
                        raft::Task *task )
    {
        auto &out( bufOut[ "0" ].allocate< type_t >( task ) );
        out.push_back( 1 );
        out.push_back( 2 );
        out.push_back( 3 );
        bufOut[ "0" ].send( task );
        return( raft::kstatus::stop );
    } );
    lambda_kernel s( 0, 1, v_lambda );
    raft::DAG dag;
    dag += s >> print;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
