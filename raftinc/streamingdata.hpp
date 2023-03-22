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
#include "raftinc/exceptions.hpp"
#include "raftinc/task.hpp"
#include "raftinc/defs.hpp"
#include "raftinc/singleton.hpp"
#include "raftinc/allocate/allocate.hpp"

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
    DataRef &set( T &data_ref )
    {
        ref = reinterpret_cast< void * >( &data_ref );
        return *this;
    }
    operator bool() const
    {
        return nullptr != ref;
    }
private:
    void *ref = nullptr;
};


class StreamingData
{
public:

    enum Direction
    {
        IN,
        OUT
    };

    StreamingData( Task *t = nullptr, Direction dir = IN ) :
        task( t ), direction( dir )
    {
    }

    template< class T >
    void setT( const port_key_t &name, T &data_ref )
    {
        store.insert( std::make_pair( name, DataRef() ) );
        store[ name ].set< T >( data_ref );
    }

    template< class T >
    T &getT( const port_key_t &name )
    {
        return store[ name ].get< T >();
    }

    void set( const port_key_t &name, const DataRef &ref )
    {
        store.insert( std::make_pair( name, ref ) );
    }

    void set( const port_key_t &name, DataRef &&ref )
    {
        store.insert( std::make_pair( name, ref ) );
    }

    DataRef &get( const port_key_t &name )
    {
        return store[ name ];
    }

    StreamingData &select( const port_name_t &name )
    {
#if STRING_NAMES
        iter = store.find( name );
        Singleton::allocate()->select( task, name, IN == direction );
#else
        iter = store.find( name.val );
        Singleton::allocate()->select( task, name.val, IN == direction );
#endif
        return *this;
    }

    StreamingData &operator[]( const port_name_t &name )
    {
#if STRING_NAMES
        iter = store.find( name );
#else
        iter = store.find( name.val );
#endif
        if( store.end() == iter )
        {
#if STRING_NAMES
            Singleton::allocate()->select( task, name, IN == direction );
#else
            Singleton::allocate()->select( task, name.val, IN == direction );
#endif
        }
        return *this;
    }

    StreamingData &at( const port_name_t &name )
    {
#if STRING_NAMES
        auto iter( store.find( name ) );
#else
        auto iter( store.find( name.val ) );
#endif
        if( store.end() == iter )
        {
            std::stringstream ss;
#if STRING_NAMES
            ss << "Data not found for " << name << std::endl;
#else
            ss << "Data not found for " << name.val << std::endl;
#endif
            throw DataNotFoundException( ss.str() );
        }
        return this->operator[]( name );
    }

    template< class T >
    void pop( T &item )
    {
        if( store.end() == iter )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPop( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        item = iter->second.get< T >();
    }

    template< class T >
    T &peek()
    {
        if( store.end() == iter )
        {
            return Singleton::allocate()->taskPeek( task ).get< T >();
        }
        return iter->second.get< T >();
    }

    void recycle()
    {
        Singleton::allocate()->taskRecycle( task );
    }

    template< class T >
    void push( T &&item )
    {
        if( store.end() == iter )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPush( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        iter->second.get< T >() = item;
        used.insert( *iter );
        store.erase( iter );
    }

    template< class T >
    void push( T &item )
    {
        if( store.end() == iter )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPush( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        iter->second.get< T >() = item;
        used.insert( *iter );
        store.erase( iter );
    }

    template< class T >
    T &allocate()
    {
        if( store.end() == iter )
        {
            return Singleton::allocate()->taskAllocate( task ).get< T >();
        }
        return iter->second.get< T >();
    }

    void send()
    {
        if( store.end() == iter )
        {
            Singleton::allocate()->taskSend( task );
            return;
        }
        used.insert( *iter );
        store.erase( iter );
    }

    std::unordered_map< port_key_t, DataRef >::iterator begin()
    {
        return store.begin();
    }

    std::unordered_map< port_key_t, DataRef >::iterator end()
    {
        return store.end();
    }

    bool has( const port_key_t &name ) const
    {
        return store.end() != store.find( name );
    }

    std::unordered_map< port_key_t, DataRef > &getUsed()
    {
        return used;
    }

private:
    Task *task;
    Direction direction;
    std::unordered_map< port_key_t, DataRef > store;
    std::unordered_map< port_key_t, DataRef >::iterator iter;
    std::unordered_map< port_key_t, DataRef > used;
}; /** end StreamingData decl **/

} /** end namespace raft */
#endif /* END RAFT_STREAMINGDATA_HPP */
