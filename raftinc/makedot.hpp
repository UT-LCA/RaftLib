/**
 * makedot.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 13:40:09 2023
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
#ifndef RAFT_MAKEDOT_HPP
#define RAFT_MAKEDOT_HPP  1
#include <ostream>
#include <sstream>

#include "raftinc/defs.hpp"

namespace raft
{

/**
 * forward declaration
 */
template< class CONTAINER >
class DotMaker;

/** some helper functions for recursive field adding **/
template < class T >
std::string field_helper( const T &&str ) {
    std::stringstream ss;
    ss << str;
    return( std::move< std::string >( ss.str() ) );
}

template < class T, class... TS >
std::string field_helper( const T &&str, TS&&... str_list ) {
    std::stringstream ss;
    ss << str << ", " <<
        field_helper< TS... >( std::forward< TS >( str_list )... );
    return( std::move< std::string >( ss.str() ) );
}

template < class CONTAINER, class T = raft::DotMaker< CONTAINER > >
void make_dot( std::ostream &os, CONTAINER &all_ks, CONTAINER &src_ks )
{
    T maker( all_ks, src_ks );
    maker.run( os );
    return;
}

template< class CONTAINER >
class DotMaker
{
public:
    DotMaker( CONTAINER& all_ks, CONTAINER& src_ks ) :
        all_kernels( all_ks ), source_kernels( src_ks )
    {
        auto *height_env( std::getenv( "GEN_DOT_HEIGHT" ) );
        if( height_env != nullptr )
        {
            height = std::stoi( height_env );
        }
        auto *width_env( std::getenv( "GEN_DOT_WIDTH" ) );
        if( width_env != nullptr )
        {
            width = std::stoi( width_env );
        }
    }

    virtual ~DotMaker() = default;

    /**
     * call this to kick off graph construction,
     * will dump the file to the stream with
     * a proper "digraph" format, override this
     * function or one of the "protected" functions
     * below to redefine this behavior for sub-classes
     * if you want differing behavior.
     */
    virtual void run( std::ostream &stream )
    {
        //make dot header
        generate_preamble( stream );
        //run over all kernels to make header
        generate_vertex_list( stream );
        //run through graph edges to build edge list
        generate_edge_list( stream );
        //dot close format
        generate_close( stream );
        return;
    }

protected:

    virtual void generate_preamble( std::ostream &stream )
    {
        stream << "digraph G{\n";
        stream << "\t";
        stream << generate_field( "size", height, width ) << ";\n";
        return;
    }
    virtual void generate_vertex_list( std::ostream &stream )
    {
        for( auto &k /** kernel **/ : all_kernels /** in container **/ )
        {
            stream << "\t" << k->getId();
            stream << "[";
            stream << generate_field( "label", common::printClassName( *k ) );
            stream << ", ";
            stream << generate_field( "shape", "ellipse" );
            stream << ", ";
            stream << generate_field( "fontname", "Helvetica" );
            stream << "];\n";
        }
        return;
    }
    virtual void generate_edge_list( std::ostream &stream )
    {
        (void) stream;
        auto dot_func = [&]( PortInfo &a,
                             PortInfo &b,
                             void *data ) -> void
        {
            auto *stream_ptr( reinterpret_cast< std::ostream* >( data ) );
            (*stream_ptr) << "\t" << a.my_kernel->getId() << " -> ";
            (*stream_ptr) << b.my_kernel->getId();
            (*stream_ptr) << "[";
            std::stringstream ss;
            ss << a.my_name << " to " << b.my_name  << " (";
            ss << common::printClassNameFromStr( a.type.name() ) + ")";
            ss << "\n";
            (*stream_ptr) << generate_field( "label", ss.str() );
            (*stream_ptr) << "];\n";
        };

        GraphTools::BFS( source_kernels, dot_func, (void*) &stream );
        return;
    }
    virtual void generate_close( std::ostream &stream )
    {
        stream << "}\n";
        return;
    }


    template< class... TS > static
    std::string generate_field( const std::string &&name, TS&&... str_list )
    {
        return( std::move< std::string >( name + "=\"" + field_helper< TS... >(
                        std::forward< TS >( str_list )... ) + "\"" ) );
    }

    int height = 10;
    int width  = 10;
private:
    /**
     * borrowing the list of all source kernels,
     * remember, if you want updates over time
     * of what the graph looks like, this will need
     * to be used in a lock-safe manner. For multi-
     * process applications we'll also need similar
     * functionality at the ``oar'' level in order
     * to propagate the graph.
     */
    CONTAINER &all_kernels;
    CONTAINER &source_kernels;
};

} /** end namespace raft **/

#endif /* END RAFT_MAKEDOT_HPP */
