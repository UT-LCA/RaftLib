/**
 * parallelk.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:20:21 2023
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
#ifndef RAFT_PARALLELK_HPP
#define RAFT_PARALLELK_HPP  1
#include "kernel.hpp"

namespace raft
{

class DAG;

class parallel_k : public Kernel
{
public:
    parallel_k() : Kernel()
    {
    }

    virtual ~parallel_k()
    {
    }

protected:
    /**
     * inc_input - adds an input port named based on an index
     * incremented automatically.
     */
    template < class T >
    std::size_t inc_input()
    {
        const auto portid( port_name_index++ );

#ifdef STRING_NAMES
        add_input< T >( std::to_string( portid ) );
#else
        /**
         * if not strings, the addPort function expects a port_key_name_t
         * struct, so, we have to go and add it.
         */
        add_input< T >( raft::port_key_name_t( portid, std::to_string(
                        portid ) ) );
#endif
        return( portid );
    }

    /**
     * inc_output - adds an output port named based on an index
     * incremented automatically.
     */
    template < class T >
    std::size_t inc_output()
    {
        const auto portid( port_name_index++ );

#ifdef STRING_NAMES
        add_output< T >( std::to_string( portid ) );
#else
        /**
         * if not strings, the addPort function expects a port_key_name_t
         * struct, so, we have to go and add it.
         */
        add_output< T >( raft::port_key_name_t( portid, std::to_string(
                         portid ) ) );
#endif
        return( portid );
    }

    std::size_t port_name_index = 0;
    friend class DAG;
};

} /** end namespace raft */
#endif /* END RAFT_PARALLELK_HPP */
