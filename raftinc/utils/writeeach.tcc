/**
 * write_each.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:09:46 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2014 Jonathan Beard
 * Copyright 2021 Jonathan Beard
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
#ifndef RAFT_UTILS_WRITEEACH_TCC
#define RAFT_UTILS_WRITEEACH_TCC  1
#include <iterator>
#include <raft>
#include <cstddef>
#include <typeinfo>
#include <functional>
#include <type_traits>
#include <cassert>

namespace raft{


template < class T, class BackInsert > class writeeach : public parallel_k
{
public:
    writeeach( BackInsert &bi ) : parallel_k(),
                                  inserter( bi )
    {
        inc_input< T >();
    }

    writeeach( const writeeach &other ) : parallel_k(),
                                          inserter( other.inserter )
    {
        /** add a single port **/
        inc_input< T >();
    }

    virtual ~writeeach() = default;


    virtual bool pop( Task *task, bool dryrun )
    {
        return task->pop( "0", dryrun );
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return true;
    }

    virtual kstatus::value_t compute( StreamingData &dataIn,
                                      StreamingData &bufOut )
    {
        ( *inserter ) = dataIn[ "0" ].peek< T >();
        dataIn[ "0" ].recycle();
        /** hope the iterator defined overloaded ++ **/
        ++inserter;
        return( kstatus::proceed );
    }
private:
    BackInsert inserter;
};

template < class T, class BackInsert >
static
writeeach< T, BackInsert >
write_each( BackInsert &&bi )
{
    return( writeeach< T, BackInsert >( bi ) );
}

} /** end namespace raft **/
#endif /* END RAFT_UTILS_WRITEEACH_TCC */
