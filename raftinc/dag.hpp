/**
 * dag.hpp - build a Directed Acyclic Graph of kernels
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Feb 22 18:00:00 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2017 Jonathan Beard
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
#ifndef RAFT_DAG_HPP
#define RAFT_DAG_HPP  1
#include <cstdlib>
#include <cassert>
#include <sstream>
#include <map>
#include <vector>

#include "raftinc/defs.hpp"
#include "raftinc/kpair.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/exceptions.hpp"
#include "raftinc/makedot.hpp"

namespace raft
{

class DAG
{
public:
    DAG() {}

    virtual ~DAG() = default;

    template< class RUNTIME >
    void exe()
    {
        /** check types, ensure all are linked **/
        check_edges();

        auto *dot_graph_env = std::getenv( "GEN_DOT" );
        if( dot_graph_env != nullptr )
        {
            std::ofstream of( dot_graph_env );
            make_dot( of, kernels, source_kernels );
            of.close();
            auto *dot_graph_exit = std::getenv( "GEN_DOT_EXIT" );
            if( dot_graph_exit != nullptr )
            {
                const auto dot_exit_val( std::stoi( dot_graph_exit ) );
                if( dot_exit_val == 1 )
                {
                    exit( EXIT_SUCCESS );
                }
                //else continue
            }
        }

        RUNTIME runtime( *this );
        runtime.run();

        return;
    }

    void operator+=( Kpair &p )
    {
        Kpair * const pair( &p );
        assert( pair != nullptr );

        /** start at the head, go forward **/
        Kpair *next = pair->head;
        assert( next != nullptr );

        while( nullptr != next )
        {
            auto &pi( connect2( next->src, next->src_name,
                                next->dst, next->dst_name ) );
            kernels.insert( next->dst );
            if( 0 == next->dst->output.size() )
            {
                sink_kernels.insert( next->dst );
            }
            prioritized_ports[ next->getPriorityFactor() ].push_back( &pi );
            next = next->next;
        }
        kernels.insert( pair->head->src );
        if( 0 == pair->head->src->input.size() )
        {
            source_kernels.insert( pair->head->src );
        }
    }

    kernelset_t &getKernels()
    {
        return kernels;
    }

    kernelset_t &getSourceKernels()
    {
        return source_kernels;
    }

    kernelset_t &getSinkKernels()
    {
        return sink_kernels;
    }

    const std::vector< const PortInfo* > &getPortsAtPriority( int p ) const
    {
        return prioritized_ports.at( p );
    }

protected:

    /**
     * connect2 - Connect two kernels with their specified ports.
     * @param   src - Kernel*
     * @param   src_name - const port_name_t&
     * @param   dst - Kernel*
     * @param   dst_name - const port_name_t&
     * @return  PortInfo& src_port_info, which should have all info of the edge
     */
    const PortInfo &connect2( Kernel *src, const port_name_t &src_name,
                              Kernel *dst, const port_name_t &dst_name )
    {
        const PortInfo &src_port_info ( src->getOutput( src_name ) );
        const PortInfo &dst_port_info ( dst->getInput( dst_name ) );

        if( src_port_info.type != dst_port_info.type )
        {
            std::stringstream ss;
            ss << "Error found when attempting to connect kernel \"" <<
                common::printClassName( src ) <<  "\" with port [" <<
                src_port_info.my_name << "] to kernel \"" <<
                common::printClassName( dst ) << "\" with port [" <<
                dst_port_info.my_name << "], their types must match. " <<
                "currently their types are (" <<
                common::printClassNameFromStr( src_port_info.type.name() ) <<
                " -and- " <<
                common::printClassNameFromStr( dst_port_info.type.name() ) <<
                ").";
            throw PortTypeMismatchException( ss.str() );
        }

        src->set_output( src_port_info.my_name, dst_port_info );
        dst->set_input( dst_port_info.my_name, src_port_info );

        return src_port_info;
    }

    /**
     * check_edges - check all kernels in the graph to look for
     * disconnected edges.
     * @throws PortException - thrown if an unconnected port is found.
     */
    void check_edges()
    {
        /**
         * NOTE: will throw an error that we're not catching here
         * if there are unconnected edges...this is something that
         * a user will have to fix.  Otherwise will return with no
         * errors.
         */
        /**
         * the old version used a BFT, this one uses a much
         * more efficient linear search through the graph
         */
        for( Kernel *k : kernels )
        {
            /**
             * NOTE: This will throw an exception
             * of type PortUnconnectedException
             */
            try {
                k->allConnected();
            } catch(...)
            {
                throw;
            }
        }
        return;
    }

private:

    kernelset_t kernels;
    kernelset_t source_kernels; // kernels having no import
    kernelset_t sink_kernels; // kernels having no output
    std::map< int, std::vector< const PortInfo* > > prioritized_ports;

}; /** end DAG decl **/

} /** end namespace raft **/
#endif /* END RAFT_DAG_HPP */
