/**
 * read_each.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:12:46 2023
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
#ifndef RAFT_UTILS_READEACH_TCC
#define RAFT_UTILS_READEACH_TCC  1
#include <iterator>
#include <functional>
#include <map>
#include <cstddef>

#include <raft>

namespace raft{


template < class T,
           class Iterator > class readeach : public parallel_k
{
public:
    readeach( Iterator &b,
              Iterator &e ) : parallel_k(),
                              begin( b ),
                              end( e )
    {
        inc_output< T >();
    }


    readeach( const readeach &other ) : parallel_k(),
                                        begin( std::move( other.begin ) ),
                                        end  ( std::move( other.end ) )
    {
        inc_output< T >();
    }

    virtual ~readeach() = default;


    virtual bool pop( Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return task->allocate( "0", dryrun );
    }

    virtual kstatus::value_t compute( StreamingData &dataIn,
                                      StreamingData &bufOut,
                                      Task *task )
    {
        bufOut[ "0" ].allocate< T >( task ) = *begin;
        bufOut[ "0" ].send( task );
        ++begin;
        return ( end == begin ) ? kstatus::stop : kstatus::proceed;
    }

private:
    Iterator       begin;
    const Iterator end;
}; /** end template readeach **/

template < class T,
           class Iterator >
static
raft::readeach< T, Iterator >
read_each( Iterator &&begin,
           Iterator &&end )
{
    return( readeach< T, Iterator >( begin,
                                     end  ) );
}


} /** end namespace raft **/
#endif /* END RAFT_UTILS_READEACH_TCC */
