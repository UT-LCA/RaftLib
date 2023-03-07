/**
 * ringbufferbase.tcc - the base class for RingBuffer. Auto-
 * specialization based on whether the data type could fit
 * into cacheline or not happens at this level.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Thu Mar 07 13:49:52 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2014 Jonathan Beard
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
#ifndef RAFT_ALLOCATE_RINGBUFFERBASE_TCC
#define RAFT_ALLOCATE_RINGBUFFERBASE_TCC  1

#include <array>
#include <cstdlib>
#include <cassert>
#include <thread>
#include <cstring>
#include <iostream>
#include <cstddef>

#include "rafttypes.hpp"
#include "allocate/buffer/pointer.hpp"
#include "allocate/buffer/blocked.hpp"
#include "allocate/buffer/buffertypes.hpp"
#include "allocate/buffer/inline_traits.tcc"
#include "allocate/fifoabstract.tcc"
#include "allocate/datamanager.tcc"

namespace raft
{

class Kernel;

/**
 * RingBufferBase - the general base template for ringbuffer
 * real implementations are the specializations in the .tcc
 * files included below.
 */
template < class T, Buffer::Type::value_t B, class Enable = void >
class RingBufferBase
{
public:
    RingBufferBase() = default;
    virtual ~RingBufferBase() = default;
};

} /** end namespace raft **/

/** implementation that uses malloc/jemalloc/tcmalloc **/
#include "allocate/ringbufferheap.tcc"
/** heap implementation, uses thread shared memory or SHM **/
#include "allocate/ringbuffershm.tcc"
/** infinite dummy implementation, can use shared memory or SHM **/
#include "allocate/ringbufferinfinite.tcc"

#endif /* END RAFT_ALLOCATE_RINGBUFFERBASE_TCC */
