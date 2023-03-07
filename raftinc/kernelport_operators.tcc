/**
 * kernelport_operators.tcc -
 * @author: Qinzhe Wu
 * @version: 23 Feb 2023
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
#ifndef RAFT_KERNELPORT_OPERATORS_TCC
#define RAFT_KERNELPORT_OPERATORS_TCC  1

#include "kernelport.hpp"
#include "kpair.hpp"

template < int N >
raft::Kpair&
operator >> ( raft::KernelPortTemplate < N > &a, raft::Kernel *b )
{
    auto *ptr( new raft::Kpair( a, b ) );
    return( *ptr );
}

template < int N >
raft::Kpair&
operator >> ( raft::KernelPortTemplate < N > &a, raft::KernelPort &b )
{
    auto *ptr( new raft::Kpair( a, b ) );
    return( *ptr );
}

template < int N >
raft::Kpair&
operator >> ( raft::KernelPortTemplate < N > &a, raft::Kpair &b )
{
    auto *ptr( new raft::Kpair( a, b ) );
    return( *ptr );
}

#endif /* END RAFT_KERNELPORT_OPERATORS_TCC */
