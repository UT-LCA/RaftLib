/**
 * kernelport.hpp -
 * @author: Qinzhe Wu
 * @version: Thu 23 02 10:29:00 2023
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
#ifndef RAFT_KERNELPORT_HPP
#define RAFT_KERNELPORT_HPP  1

#include <stack>

#include "raftinc/rafttypes.hpp"

namespace raft
{

class Kernel;

/**
 * It is hacky to make it a template. This allows me to define the stream
 * operator as template functions in the header file kernelport_operators.tcc
 * Those operator functions are not included in this file because the
 * operator functions depend on Kpair so must be defined after Kpair class.
 * This structure holds the metadata of Kernel pointer and port name.
 * In order to define the stream operators in Kpair class and Kernel class
 * the definition of this strcture must go first.
 */
template< int N = 0 >
struct KernelPortTemplate
{
    Kernel *kernel;
    std::stack< port_name_t > portnames;
    KernelPortTemplate() : kernel( nullptr )
    {
    }
    KernelPortTemplate( Kernel *k, const port_name_t &p ) : kernel( k )
    {
        portnames.push( p );
    }
    KernelPortTemplate &operator[]( const port_name_t &p )
    {
        portnames.push( p );
        return *this;
    }
    port_name_t pop_name()
    {
        if( 0 == portnames.size() )
        {
            return null_port_value;
        }
        else
        {
            port_name_t ans = portnames.top();
            portnames.pop();
            return ans;
        }
    }
};

using KernelPort = KernelPortTemplate< 0 >;

};

#endif /* END RAFT_KERNELPORT_HPP */
