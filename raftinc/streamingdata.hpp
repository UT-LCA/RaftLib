/**
 * streamingdata.hpp - the class that packages input data for a
 * kernel's computation and also output data.
 * @author: Qinzhe Wu
 * @version: Sun Feb 26 17:11:00 2023
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
#ifndef RAFT_STREAMINGDATA_HPP
#define RAFT_STREAMINGDATA_HPP  1

#include <string>
#include <sstream>
#include "exceptions.hpp"
#include "defs.hpp"

namespace raft {

class DataRef
{
public:
    DataRef() = default;
    DataRef( const DataRef &other ) : ref( other.ref ) {}
    template< class T >
    T &get()
    {
        auto *ptr( reinterpret_cast< T* >( ref ) );
        return *ptr;
    }
    template< class T >
    void set( T &data_ref )
    {
        ref = reinterpret_cast< void * >( &data_ref );
    }
private:
    void *ref = nullptr;
};


class StreamingData
{
public:
    StreamingData() = default;
    DataRef &operator[]( const port_name_t &name )
    {
        auto iter( store.find( name ) );
        if( store.end() == iter )
        {
            std::stringstream ss;
            ss << "Data not found for " << name << std::endl;
            throw DataNotFoundException( ss.str() );
        }
        touched.insert( name );
        return iter->second;
    }
    template< class T >
    void set( const port_name_t &name, T &data_ref )
    {
        store.insert( std::make_pair( name, DataRef() ) );
        store[ name ].set< T >( data_ref );
    }

    template< class T >
    T &get( const port_name_t &name )
    {
        return store[ name ].get< T >();
    }

    void set( const port_name_t &name, const DataRef &ref )
    {
        store.insert( std::make_pair( name, ref ) );
    }

    DataRef &get( const port_name_t &name )
    {
        return store[ name ];
    }

    std::unordered_map< port_name_t, DataRef >::iterator begin()
    {
        return store.begin();
    }

    std::unordered_map< port_name_t, DataRef >::iterator end()
    {
        return store.end();
    }

    bool kernelTouched( const port_name_t &name )
    {
        return touched.end() != touched.find( name );
    }

    void clearTouchedMarks()
    {
        touched.clear();
    }

private:
    std::unordered_map< port_name_t, DataRef > store;
    std::unordered_set< port_name_t > touched;
    bool satisfied = false; /* whether enough to run the kernel compute() */
}; /** end StreamingData decl **/

} /** end namespace raft */
#endif /* END RAFT_STREAMINGDATA_HPP */
