/**
 * search.tcc - a kernel does search
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 13:02:44 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2015 Jonathan Beard
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
#ifndef RAFT_UTILS_SEARCH_TCC
#define RAFT_UTILS_SEARCH_TCC  1

#include <utility>
#include <cstddef>
#include <raft>
#include <algorithm>

namespace raft
{

using match_t = std::pair< std::size_t /** start **/,
                           std::size_t /** end   **/>;

enum searchalgo { stdlib, pcre };

template < class T, searchalgo ALGO > class search;

template < class T > class search< T, stdlib > : public raft::Kernel
{
public:
    search( const std::string && term ) : raft::Kernel(),
                                          term_length( term.length() ),
                                          term( term )
    {
        add_input< T >( "0" );
        add_output< match_t >( "1" );
    }

    search( const std::string &term ) : raft::Kernel(),
                                        term_length( term.length() ),
                                        term( term )
    {
        add_input< T >( "0" );
        add_output< match_t >( "1" );
    }

    virtual ~search() = default;

    virtual bool pop( Task *task, bool dryrun )
    {
        return task->pop( "0", dryrun );
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return task->allocate( "1", dryrun );
    }

    virtual raft::kstatus::value_t compute( StreamingData &dataIn,
                                            StreamingData &bufOut,
                                            Task *task )
    {
        auto &chunk( dataIn[ "0" ].peek< T >( task ) );
        auto it( chunk.begin() );
        do
        {
            it = std::search( it, chunk.end(),
                              term.begin(), term.end() );
            if( it != chunk.end() )
            {
                const std::size_t loc( it.location() + chunk.start_position );
                const std::size_t end( loc + term_length );
                bufOut[ "1" ].push< match_t >( std::make_pair( loc, end ), task );
                it += 1;
            }
            else
            {
                break;
            }
        }
        while( true );
        dataIn[ "0" ].recycle( task );
        return( raft::kstatus::proceed );
    }
private:
   const std::size_t term_length;
   const std::string term;
};



}

#endif /* END RAFT_UTILS_SEARCH_TCC */
