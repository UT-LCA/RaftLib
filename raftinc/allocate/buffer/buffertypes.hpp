/**
 * buffertypes.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:50:21 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2020 Jonathan Beard
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
#ifndef RAFT_BUFFERTYPES_HPP
#define RAFT_BUFFERTYPES_HPP 1
#include <array>

namespace Buffer
{

namespace Type
{
enum value_t {
    Heap,
    SharedMemory,
    TCP,
    Infinite,
    N
};

static constexpr std::array<  const char[20] , N >
Prints = {{ "Heap", "SharedMemory", "TCP", "Infinite" }};
} /* end namespace Type */


#if defined __APPLE__ || defined __linux

/**
 * enum used by SharedMemory for letting this queue know which
 * side we're allocating
 */
enum Direction { Producer, Consumer };

#endif

}

#endif /* end RAFT_BUFFERTYPES_HPP */
