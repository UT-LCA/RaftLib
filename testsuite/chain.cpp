/**
 * random.cpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar  8 23:46:14 2023
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
 #include <raftrandom>
 #include <cstdint>
 #include <iostream>
 #include <raftio>


 int
 main()
 {
   using namespace raft;
   using type_t = std::uint32_t;
   const static auto send_size( 10 );
   using gen = random_variate< std::default_random_engine,
                               std::uniform_int_distribution,
                               type_t >;
   using p_out = raft::print< type_t, '\n' >;
   using sub = raft::lambdak< type_t >;

   std::vector< type_t > output;

   const static auto min( 0 );
   const static auto max( 100 );
   gen g( send_size, min, max );

   p_out print;

   auto l_sub( [&]( raft::StreamingData &dataIn,
                    raft::StreamingData &bufOut )
   {
       std::uint32_t a;
       dataIn[ "0" ].pop( a );
       bufOut[ "0" ].push( a - 10 );
       return( raft::kstatus::proceed );
   } );
   auto l_pop( [&]( raft::Task *task,
                    bool dryrun )
   {
       return task->pop( "0", dryrun );
   } );

   sub s( 1, 1, l_sub, l_pop );

   raft::DAG dag;
   dag += g >> ( s >> print );

   dag.exe< raft::RuntimeFIFOGroup >();

   return( EXIT_SUCCESS );
 }
