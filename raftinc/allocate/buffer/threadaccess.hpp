/**
 * threadaccess.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 13:26 2023
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
#ifndef RAFTTHREADACCESS_HPP
#define RAFTTHREADACCESS_HPP  1
#include <cstdint>

#include "raftinc/allocate/buffer/internaldefs.hpp"

namespace Buffer
{

using byte_t = std::uint8_t;
using key_t = byte_t;
/**
 * access_key - each one of these is to be used as a
 * key for  buffer access functions.  Everything <=
 * push is expected to be a write type function, everything
 * else is expected to be a read type operation.
 */
enum access_key : key_t { allocate       = 0,
                          allocate_range = 1,
                          push           = 3,
                          recycle        = 4,
                          pop            = 5,
                          peek           = 6,
                          size           = 7,
                          N };

struct ALIGN( L1D_CACHE_LINE_SIZE ) ThreadAccess
{
    ThreadAccess() = default;

    constexpr ThreadAccess( const ThreadAccess  &other )
    {
        (this)->whole = other.whole;
    }

    constexpr ThreadAccess& operator = (const ThreadAccess &other ) noexcept
    {
        /** ignore the case of 'this' == other **/
        (this)->whole = other.whole;
        return( *this );
    }

    /**
     * technically not needed but this makes debugging
     * a bit easier. - jcb July 2019
     */
    ~ThreadAccess(){ (this)->whole = 0; }

    union
    {
        std::uint64_t whole = 0;
        key_t         flag[ 8 ];
    };

    byte_t    padding[ L1D_CACHE_LINE_SIZE - 8 /** padd to cache line **/ ]   = {};
};

} /** end namespace buffer **/

#endif /* END RAFTTHREADACCESS_HPP */
