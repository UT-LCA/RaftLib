/**
 * ringbufferheap.tcc - specializations of RingBufferBase with
 * Heap as the buffer data type. The content of this file really
 * should be inside ringbufferbase.tcc because it is defining
 * class RingBufferBase. It is split into this file to avoid
 * making ringbufferbase.tcc too long.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 13:50:56 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFERHEAP_TCC
#define RAFT_ALLOCATE_RINGBUFFERHEAP_TCC  1

#include "allocate/buffer/buffertypes.hpp"
#include "allocate/ringbuffer_commonbase.tcc"

namespace raft
{

template < class T >
class RingBufferBase< T, Buffer::Type::Heap > :
    public RingBufferCommonBase< T, Buffer::Type::Heap >
{
public:
    RingBufferBase() : RingBufferCommonBase< T, Buffer::Type::Heap >() {}
};

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFERHEAP_TCC */
