/**
 * streamingData.cpp -
 * @author: Qinzhe Wu
 * @version: Thu Mar 02 20:31:00 2023
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
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <iostream>
#include <typeindex>
#include "rafttypes.hpp"
#include "streamingdata.hpp"

#define CHAR_ARR_LEN 16

using type0_t = std::int32_t;
using type1_t = char[ CHAR_ARR_LEN ];
using type2_t = raft::DataRef;

int
main()
{
    raft::StreamingData sd;
    type0_t val0( 1234 );
    type1_t val1 = "Hello world\n";
    type2_t val2 = raft::DataRef();

    val2.set< type0_t >( val0 );

#if STRING_NAMES
    sd.setT< type0_t >( "int32_t"_port, val0 );
    sd.setT< type1_t >( "char[16]"_port, val1 );
    sd.setT< type2_t >( "DataRef"_port, val2 );
#else
    sd.setT< type0_t >( "int32_t"_port.val, val0 );
    sd.setT< type1_t >( "char[16]"_port.val, val1 );
    sd.setT< type2_t >( "DataRef"_port.val, val2 );
#endif

#if STRING_NAMES
    auto &ref0( sd.getT< type0_t >( "int32_t"_port ) );
    auto &ref1( sd.getT< type1_t >( "char[16]"_port ) );
    auto &ref2( sd.getT< type2_t >( "DataRef"_port ) );
#else
    auto &ref0( sd.getT< type0_t >( "int32_t"_port.val ) );
    auto &ref1( sd.getT< type1_t >( "char[16]"_port.val ) );
    auto &ref2( sd.getT< type2_t >( "DataRef"_port.val ) );
#endif
    auto &ref20( ref2.get< type0_t >() );

    if( typeid( val0 ) != typeid( ref0 ) ||
        typeid( val1 ) != typeid( ref1 ) ||
        typeid( val2 ) != typeid( ref2 ) ||
        typeid( val0 ) != typeid( ref20 )
        )
    {
        std::cerr << "failed exit\n";
        exit( EXIT_FAILURE );
    }

    if( val0 != ref0 ||
        val0 != ref20
      )
    {
        std::cerr << "failed exit\n";
        exit( EXIT_FAILURE );
    }

    for( int i( 0 ); CHAR_ARR_LEN > i; ++i )
    {
        if( val1[ i ] != ref1[ i ] )
        {
            std::cerr << "failed exit\n";
            exit( EXIT_FAILURE );
        }
    }

    return( EXIT_SUCCESS );
}
