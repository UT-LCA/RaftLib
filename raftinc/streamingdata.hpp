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

    using store_map_t = std::unordered_map< port_key_t, DataRef >;

    enum Type : std::uint8_t
    {
        IN         = 0x0,
        OUT        = 0x1,
        /* SINGLE: single port could avoid indexing with
         * port name and looking up the container */
        SINGLE_IN  = 0x2,
        SINGLE_OUT = 0x3,
        /* 1PIECE: prefetched from message queue, or stay
         * as 1 piece output for oneshot tasks, rather
         * than entering a message queue */
        IN_1PIECE  = 0x6,
        OUT_1PIECE = 0x7
    };

    bool isInput() const
    {
        return 0 == ( type & 0x1 );
    }

    bool isSingle() const
    {
        return 0 != ( type & 0x2 );
    }

    bool is1Piece() const
    {
        return 0 != ( type & 0x4 );
    }


    StreamingData( Task *t = nullptr, Type tt = IN ) :
        task( t ), type( tt )
    {
        if( !isSingle() )
        {
            store = new store_map_t;
            used = new store_map_t;
        }
    }

    ~StreamingData()
    {
        if( nullptr != store )
        {
            delete store;
        }
        if( nullptr != used )
        {
            delete used;
        }
    }

    template< class T >
    void setT( const port_key_t &name, T &data_ref )
    {
        if( isSingle() )
        {
            single_store.set< T >( data_ref );
            single_store_valid = true;
        }
        else
        {
            store->insert(
                    std::make_pair( name, DataRef().set< T >( data_ref ) ) );
        }
    }

    template< class T >
    T &getT( const port_key_t &name )
    {
        return isSingle() ?
            single_store.get< T >() : store->at( name ).get< T >();
    }

    void set( const port_key_t &name, const DataRef &ref )
    {
        if( isSingle() )
        {
            single_store = ref;
            ref_ptr = &single_store;
            single_store_valid = true;
        }
        else
        {
            auto p( store->insert( std::make_pair( name, ref ) ) );
            iter = p.first; /* std::pair<iterator, bool> */
            ref_ptr = &iter->second;
        }
    }

    void set( const port_key_t &name, DataRef &&ref )
    {
        if( isSingle() )
        {
            single_store = ref;
            ref_ptr = &single_store;
            single_store_valid = true;
        }
        else
        {
            auto p( store->insert( std::make_pair( name, ref ) ) );
            iter = p.first; /* std::pair<iterator, bool> */
            ref_ptr = &iter->second;
        }
    }

    DataRef &get( const port_key_t &name )
    {
        return isSingle() ? single_store : store->at( name );
    }

    StreamingData &select( const port_name_t &name )
    {
        if( isSingle() )
        {
            ref_ptr = single_store_valid ? &single_store : nullptr;
            return *this;
        }
#if STRING_NAMES
        const auto &name_val( name );
#else
        const auto &name_val( name.val );
#endif
        iter = store->find( name_val );
        ref_ptr = ( store->end() == iter ) ? nullptr : &iter->second;
        if( ! is1Piece() )
        {
            Singleton::allocate()->select( task, name_val, isInput() );
        }
        return *this;
    }

    StreamingData &operator[]( const port_name_t &name )
    {
        return select( name );
    }

    StreamingData &at( const port_name_t &name )
    {
#if STRING_NAMES
        const auto &name_val( name );
#else
        const auto &name_val( name.val );
#endif
        auto iter( store->find( name_val ) );
        if( store->end() == iter )
        {
            std::stringstream ss;
            ss << "Data not found for " << name_val << std::endl;
            throw DataNotFoundException( ss.str() );
        }
        return select( name );
    }

    template< class T >
    void pop( T &item )
    {
        if( nullptr == ref_ptr )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPop( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        item = ref_ptr->get< T >();
        single_store_valid = false;
    }

    template< class T >
    T &peek()
    {
        if( nullptr == ref_ptr )
        {
            return Singleton::allocate()->taskPeek( task ).get< T >();
        }
        return ref_ptr->get< T >();
    }

    void recycle()
    {
        Singleton::allocate()->taskRecycle( task );
        single_store_valid = false;
    }

    template< class T >
    void push( T &&item )
    {
        if( nullptr == ref_ptr )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPush( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        ref_ptr->get< T >() = item;
        ref_ptr = nullptr;
        if( isSingle() )
        {
            single_store_valid = false;
        }
        else
        {
            used->insert( *iter );
            store->erase( iter );
        }
    }

    template< class T >
    void push( T &item )
    {
        if( nullptr == ref_ptr )
        {
            DataRef ref;
            ref.set< T >( item );
            Singleton::allocate()->taskPush( task, ref );
            return;
        }
        //TODO: it assumes assign operator is defined for T
        ref_ptr->get< T >() = item;
        ref_ptr = nullptr;
        if( isSingle() )
        {
            single_store_valid = false;
        }
        else
        {
            used->insert( *iter );
            store->erase( iter );
        }
    }

    template< class T >
    T &allocate()
    {
        if( nullptr == ref_ptr )
        {
            return Singleton::allocate()->taskAllocate( task ).get< T >();
        }
        return ref_ptr->get< T >();
    }

    void send()
    {
        if( nullptr == ref_ptr )
        {
            Singleton::allocate()->taskSend( task );
            return;
        }
        ref_ptr = nullptr;
        if( isSingle() )
        {
            single_store_valid = false;
        }
        else
        {
            used->insert( *iter );
            store->erase( iter );
        }
    }

    std::unordered_map< port_key_t, DataRef >::iterator begin()
    {
        assert( ! isSingle() );
        return store->begin();
    }

    std::unordered_map< port_key_t, DataRef >::iterator end()
    {
        assert( ! isSingle() );
        return store->end();
    }

    bool has( const port_key_t &name ) const
    {
        assert( ! isSingle() );
        return store->end() != store->find( name );
    }

    std::unordered_map< port_key_t, DataRef > &getUsed()
    {
        assert( ! isSingle() );
        return *used;
    }

    /** out2in1piece - convert a streaming data of OUT_1PIECE
     * to a streaming data of IN_1PIECE, so it could be given
     * to a consumer one shot task to use */
    StreamingData *out2in1piece()
    {
        assert( is1Piece() );
        type = IN_1PIECE;
        single_store_valid = true;
        ref_ptr = &single_store;
        return this;
    }

    bool isSent() const
    {
        assert( isSingle() && ! isInput() );
        return ! single_store_valid;
    }

private:
    Task *task;
    Type type;
    DataRef single_store;
    bool single_store_valid = false;
    DataRef *ref_ptr = nullptr;
    store_map_t::iterator iter;
    store_map_t *store = nullptr;
    store_map_t *used = nullptr;
}; /** end StreamingData decl **/

} /** end namespace raft */
#endif /* END RAFT_STREAMINGDATA_HPP */
