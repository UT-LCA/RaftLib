/**
 * print.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:26:39 2023
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
#ifndef RAFT_UTILS_PRINT_TCC
#define RAFT_UTILS_PRINT_TCC  1

#include <functional>
#include <ostream>
#include <iostream>
#include <raft>
#include <cstdlib>

namespace raft{

class printbase
{
protected:
   std::ostream *ofs = nullptr;
};


template< typename T > class printabstract : public raft::Kernel,
                                             public raft::printbase
{
public:
    printabstract() : raft::Kernel(),
                      raft::printbase()
    {
#if STRING_NAMES
        add_input< T >( "0" );
#else
        /**
         * if not strings, the addPort function expects a port_key_name_t struct,
         * so, we have to go and add it.
         */
        add_input< T >( "0"_port );
#endif
        ofs = &(std::cout);
    }

    printabstract( std::ostream &stream )  : raft::Kernel(),
                                             raft::printbase()
    {
#if STRING_NAMES
        add_input< T >( "0" );
#else
        /**
         * if not strings, the addPort function expects a port_key_name_t struct,
         * so, we have to go and add it.
         */
        add_input< T >( "0"_port );
#endif
        ofs = &stream;
    }

    virtual bool pop( Task *task, bool dryrun )
    {
#if STRING_NAMES
        return task->pop( "0", dryrun );
#else
        return task->pop( "0"_port, dryrun );
#endif
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return true;
    }
};

template< typename T, char delim = '\0' > class print : public printabstract< T >
{
public:
    print() : printabstract< T >()
    {
    }

    print( std::ostream &stream ) : printabstract< T >( stream )
    {
    }

    print( const print &other ) : print( *other.ofs )
    {
    }


    /** enable cloning **/
    //CLONE();

    /**
     * compute -
     * @return raft::kstatus::value_t
     */

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
#if STRING_NAMES
        const auto &data( dataIn.peek< T >() );
        *((this)->ofs) << data << delim;
        dataIn.recycle();
#else
        const auto &data( dataIn[ "0"_port ].peek< T >() );
        *((this)->ofs) << data << delim;
        dataIn[ "0"_port ].recycle();
#endif
        return( raft::kstatus::proceed );
    }
};


template< typename T > class print< T, '\0' > : public printabstract< T >
{
public:
    print() : printabstract< T >()
    {
    }

    print( std::ostream &stream ) : printabstract< T >( stream )
    {
    }

    print( const print &other ) : print( *other.ofs )
    {
    }

    //CLONE();


    /**
     * compute -
     * @return raft::kstatus::value_t
     */
    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
#if STRING_NAMES
        const auto &data( dataIn.peek< T >() );
        *((this)->ofs) << data;
        dataIn.recycle();
#else
        const auto &data( dataIn[ "0"_port ].peek< T >() );
        *((this)->ofs) << data;
        dataIn[ "0"_port ].recycle();
#endif
        return( raft::kstatus::proceed );
    }
};

} /* end namespace raft */
#endif /* END RAFTPRINT_TCC */
