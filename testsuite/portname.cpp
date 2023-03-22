/**
 * portname.cpp -
 * @author: Qinzhe Wu
 * @version: Wed Mar 22 14:15:00 2023
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


int
main()
{
    auto portname0( "123"_port );
    auto portname1( "101"_port );
    auto portname2( "x"_port );
    auto portname3(
            "a long long long long long long long long name does not even fit"
            " into a single line"_port
            );


#if STRING_NAMES
    if( "123" != portname0 ||
        "101" != portname1 ||
        "x" != portname2 ||
        "a long long long long long long long long name does not even fit "
        "into a single line" != portname3 )
#else
    if( 0xd448b6d79401008 != portname0.val ||
        0x4e02394170a68128 != portname1.val ||
        0x68ab6097db736aea != portname2.val ||
        0xa834d8e593546a90 != portname3.val )
#endif
    {
        std::cerr << "failed exit\n";
        exit( EXIT_FAILURE );
    }

    return( EXIT_SUCCESS );
}
