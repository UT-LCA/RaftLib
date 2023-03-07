/**
 * ringbuffershm.tcc - specializations of RingBufferBase with
 * SharedMemory as the buffer data type. The content of this
 * file really should go into ringbufferbase.tcc because it is
 * defining class RingBufferBase. It is split here to avoid
 * making ringbufferbase.tcc too long.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 13:51:58 2023
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
#ifndef RAFT_ALLOCATE_RINGBUFFERSHM_TCC
#define RAFT_ALLOCATE_RINGBUFFERSHM_TCC  1

#include "allocate/buffer/buffertypes.hpp"
#include "allocate/ringbuffer_commonbase.tcc"

namespace raft
{

template < class T >
class RingBufferBase< T, Buffer::Type::SharedMemory > :
    public RingBufferCommonBase< T, Buffer::Type::SharedMemory >
{
public:
    RingBufferBase() :
        RingBufferCommonBase< T, Buffer::Type::SharedMemory >() {}
};

} /** end namespace raft */
#endif /* END RAFT_ALLOCATE_RINGBUFFERSHM_TCC */
