/**
 * kpair.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: 07 Mar 2023
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
#ifndef RAFT_KPAIR_HPP
#define RAFT_KPAIR_HPP  1

#include "defs.hpp"
#include "kernelport.hpp"

namespace raft
{

/**
 * Forward declaration
 */
class Kernel;
class DAG;

class Kpair
{
public:
    Kpair( Kernel *a,
           Kernel *b,
           const port_name_t &pa = null_port_value,
           const port_name_t &pb = null_port_value
           ) : src( a ), dst( b ), src_name( pa ), dst_name( pb )
    {
        head = this;
    }

    Kpair( Kernel *a,
           KernelPort &b,
           const port_name_t &pa = null_port_value
           ) : Kpair( a,  b.kernel, pa, b.pop_name() ) {}

    Kpair( Kernel *a,
           Kpair &b,
           const port_name_t &pa = null_port_value
           ) : Kpair( a, b.src, pa, b.src_name )
    {
        next = &b;
        b.head = this;
    }

    Kpair( KernelPort &a,
           Kernel *b,
           const port_name_t &pb = null_port_value
           ) : Kpair( a.kernel, b, a.pop_name(), pb ) {}

    Kpair( KernelPort &a,
           KernelPort &b
           ) : Kpair( a.kernel, b.kernel, a.pop_name(), b.pop_name() ) {}

    Kpair( KernelPort &a,
           Kpair &b
           ) : Kpair( a.kernel, b.src, a.pop_name(), b.src_name )
    {
        next = &b;
        b.head = this;
    }

    Kpair( Kpair &a,
           Kernel *b,
           const port_name_t &pb = null_port_value
           ) : Kpair( a.dst, b, a.dst_name, pb )
    {
        head = a.head;
        a.next = this;
    }

    Kpair( Kpair &a,
           KernelPort &b
           ) : Kpair( a.dst, b.kernel, a.dst_name, b.pop_name() )
    {
        head = a.head;
        a.next = this;
    }

    Kpair( Kpair &a,
           Kpair &b
           ) : Kpair( a.dst, b.src, a.dst_name, b.src_name )
    {
        head = a.head;
        a.next = this;
        b.head = a.head;
        next = &b;
    }

    Kpair&
    operator >> ( Kernel &rhs )
    {
        auto *ptr( new Kpair( *this, &rhs ) );
        return( *ptr );
    }

    Kpair&
    operator >> ( Kernel *rhs )
    {
        auto *ptr( new Kpair( *this, rhs ) );
        return( *ptr );
    }

    Kpair&
    operator >> ( KernelPort &rhs )
    {
        auto *ptr( new Kpair( *this, rhs ) );
        return( *ptr );
    }

    Kpair&
    operator >> ( Kpair &rhs )
    {
        auto *ptr( new Kpair( *this, rhs ) );
        return( *ptr );
    }

protected:
    Kpair *next = nullptr;
    Kpair *head = nullptr;
    Kernel *src = nullptr;
    Kernel *dst = nullptr;
    port_name_t src_name = null_port_value;
    port_name_t dst_name = null_port_value;

    friend Kernel;
    friend DAG;
};

} /** end namespace raft */
#endif /* END RAFT_KPAIR_HPP */
