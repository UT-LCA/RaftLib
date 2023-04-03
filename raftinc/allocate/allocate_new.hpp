/**
 * allocate_new.hpp - the allocator allocates the storage by
 * new/malloc/tcache_alloc. Only serves Oneshot scheduling.
 * @author: Qinzhe Wu
 * @version: Sat Apr 01 09:17:06 2023
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
#ifndef RAFT_ALLOCATE_ALLOCATE_NEW_HPP
#define RAFT_ALLOCATE_ALLOCATE_NEW_HPP  1

#include <vector>
#include <unordered_set>

#if UT_FOUND
#include <ut>
#endif

#include "raftinc/defs.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/port_info.hpp"
#include "raftinc/exceptions.hpp"
#include "raftinc/oneshottask.hpp"
#include "raftinc/streamingdata.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/fifofunctor.hpp"

#define ALLOC_ALIGN_WIDTH L1D_CACHE_LINE_SIZE

namespace raft
{

#if UT_FOUND
inline __thread tcache_perthread __perthread_new_alloc_meta_pt;
#endif

struct BufListNode
{
    BufListNode *next = nullptr;
    PortInfo *pi = nullptr;
    DataRef ref;
};

struct TaskNewAllocMeta : public TaskAllocMeta
{
    TaskNewAllocMeta() : TaskAllocMeta(), selected_out( nullptr ) {}
    virtual ~TaskNewAllocMeta() = default;

    BufListNode outbufs;

    PortInfo *selected_out;
};

class AllocateNew : public Allocate
{
public:

    /**
     * AllocateNew - base constructor
     */
    AllocateNew() : Allocate() {}

    /**
     * destructor
     */
    virtual ~AllocateNew() = default;

    virtual DAG &allocate( DAG &dag )
    {
        (this)->ready = true;
        return dag;
    }

#if UT_FOUND
    virtual void globalInitialize()
    {
        if( ! Singleton::schedule()->doesOneShot() )
        {
            return;
        }
        slab_create( &streaming_data_slab, "streamingdata",
                     sizeof( StreamingData ), 0 );
        streaming_data_tcache = slab_create_tcache( &streaming_data_slab, 64 );
        slab_create( &new_alloc_meta_slab, "newallocmeta",
                     sizeof( TaskNewAllocMeta ), 0 );
        new_alloc_meta_tcache = slab_create_tcache( &new_alloc_meta_slab, 64 );
    }

    virtual void perthreadInitialize()
    {
        if( ! Singleton::schedule()->doesOneShot() )
        {
            return;
        }
        tcache_init_perthread( streaming_data_tcache,
                               &__perthread_streaming_data_pt );
        tcache_init_perthread( new_alloc_meta_tcache,
                               &__perthread_new_alloc_meta_pt );
    }
#endif

    virtual bool dataInReady( Task *task, const port_key_t &name )
    {
        throw MethodNotImplementdException(
                "AllocateNew::dataInReady(Task*, const port_key_t&)" );
        UNUSED( task );
        UNUSED( name );
        return true;
    }

    virtual bool bufOutReady( Task *task, const port_key_t &name )
    {
        throw MethodNotImplementdException(
                "AllocateNew::bufOutReady(Task*, const port_key_t&)" );
        UNUSED( task );
        UNUSED( name );
        return true;
    }

    virtual bool getDataIn( Task *task, const port_key_t &name )
    {
        throw MethodNotImplementdException(
                "AllocateNew::getDataIn(Task*, const port_key_t&)" );
        UNUSED( task );
        UNUSED( name );
        return true;
    }

    virtual bool getBufOut( Task *task, const port_key_t &name )
    {
        throw MethodNotImplementdException(
                "AllocateNew::getBufOut(Task*, const port_key_t&)" );
        UNUSED( task );
        UNUSED( name );
        return true;
    }

    virtual StreamingData &getDataIn( Task *task )
    {
        throw MethodNotImplementdException(
                "AllocateNew::getDataIn(Task*)" );
        auto *ptr( new StreamingData() );
        return *ptr;
    }

    virtual StreamingData &getBufOut( Task *task )
    {
        throw MethodNotImplementdException(
                "AllocateNew::getBufOut(Task*)" );
        auto *ptr( new StreamingData() );
        return *ptr;
    }

    virtual void taskInit( Task *task, bool alloc_input )
    {
        assert( ONE_SHOT == task->type );
        auto *t( static_cast< OneShotTask* >( task ) );
        oneshot_init( t, alloc_input );
    }

    virtual void registerConsumer( Task *task )
    {
        throw MethodNotImplementdException(
                "AllocateNew::registerConsumer()" );
        UNUSED( task );
    }

    virtual void taskCommit( Task *task )
    {
        oneshot_commit( static_cast< OneShotTask* >( task ) );
    }

    virtual void invalidateOutputs( Task *task )
    {
        UNUSED( task );
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        throw MethodNotImplementdException(
                "AllocateNew::taskHasInputPorts()" );
        return true;
    }

    virtual void select( Task *task, const port_key_t &name, bool is_in )
    {
        UNUSED( is_in ); /* OneShot task has input selected in StreamingData */
        auto *tmeta( static_cast< TaskNewAllocMeta* >( task->alloc_meta ) );
        auto iter( task->kernel->output.find( name ) );
        assert( task->kernel->output.end() != iter );
        tmeta->selected_out = &iter->second;
    }

    virtual void taskPop( Task *task, DataRef &item )
    {
        // oneshot task should have all input data satisfied by StreamingData
        throw MethodNotImplementdException( "AllocateNew::taskPop()" );
        UNUSED( task );
        UNUSED( item );
    }

    virtual DataRef taskPeek( Task *task )
    {
        // oneshot task should have all input data satisfied by StreamingData
        throw MethodNotImplementdException( "AllocateNew::taskPeek()" );
        UNUSED( task );
        return DataRef();
    }

    virtual void taskRecycle( Task *task )
    {
        UNUSED( task );
    }

    virtual void taskPush( Task *task, DataRef &item )
    {
        auto *tmeta( static_cast< TaskNewAllocMeta* >( task->alloc_meta ) );
        auto *functor( tmeta->selected_out->runtime_info.fifo_functor );
        auto *node( new BufListNode() );
        node->ref = functor->oneshot_allocate( item );
        node->pi = tmeta->selected_out;
        // insert into the linked list
        node->next = tmeta->outbufs.next;
        tmeta->outbufs.next = node;
    }

    virtual DataRef taskAllocate( Task *task )
    {
        auto *tmeta( static_cast< TaskNewAllocMeta* >( task->alloc_meta ) );
        auto *functor( tmeta->selected_out->runtime_info.fifo_functor );
        //TODO: merge the following two news into one tcache_alloc()
        auto *node( new BufListNode() );
        node->ref = functor->oneshot_allocate();
        node->pi = tmeta->selected_out;
        //FIXME: this assumes every allocate would be enventually sent
        // insert into the linked list
        node->next = tmeta->outbufs.next;
        tmeta->outbufs.next = node;
        return node->ref;
    }

    virtual void taskSend( Task *task )
    {
        UNUSED( task );
    }

    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           bool *is_last )
    {
        auto *tmeta( static_cast< TaskNewAllocMeta* >( task->alloc_meta ) );
        if( nullptr == tmeta->outbufs.next )
        {
            return false;
        }
        BufListNode *node = tmeta->outbufs.next;
        pi_ptr = node->pi;
        ref = node->ref;
        tmeta->outbufs.next = node->next;
        delete node;
        if( nullptr != is_last )
        {
            *is_last = ( nullptr == tmeta->outbufs.next );
        }
        return true;
    }

protected:


    inline void oneshot_init( OneShotTask *oneshot, bool alloc_input )
    {
        oneshot->stream_in = nullptr;
#if USE_UT
        auto *stream_out_ptr_tmp(
                tcache_alloc( &__perthread_streaming_data_pt ) );
        oneshot->stream_out = new ( stream_out_ptr_tmp ) StreamingData(
                oneshot, StreamingData::SINGLE_OUT );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN_1PIECE :
                             StreamingData::SINGLE_IN_1PIECE );
            auto *stream_in_ptr_tmp(
                    tcache_alloc( &__perthread_streaming_data_pt ) );
            oneshot->stream_in =
                new ( stream_in_ptr_tmp ) StreamingData( oneshot, sd_in_type );
        }
        auto *tmeta_ptr_tmp( tcache_alloc( &__perthread_new_alloc_meta_pt ) );
        oneshot->alloc_meta = new ( tmeta_ptr_tmp ) TaskNewAllocMeta();
#else
        oneshot->stream_out = new StreamingData( oneshot,
                                                 StreamingData::SINGLE_OUT );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN_1PIECE :
                             StreamingData::SINGLE_IN_1PIECE );
            oneshot->stream_in = new StreamingData( oneshot, sd_in_type );
        }
        oneshot->alloc_meta = new TaskNewAllocMeta();
#endif
        // preselect if single output port available
        if( 1 == oneshot->kernel->output.size() )
        {
            auto *tmeta(
                    static_cast< TaskNewAllocMeta* >( oneshot->alloc_meta ) );
            tmeta->selected_out = &oneshot->kernel->output.begin()->second;
        }
    }

    static inline void oneshot_commit( OneShotTask *oneshot )
    {
        if( nullptr != oneshot->stream_in )
        {
#if USE_UT
            tcache_free( &__perthread_streaming_data_pt, oneshot->stream_in );
#else
            delete oneshot->stream_in;
#endif
        }
        if( nullptr != oneshot->stream_out )
        {
#if USE_UT
            tcache_free( &__perthread_streaming_data_pt, oneshot->stream_out );
#else
            delete oneshot->stream_out;
#endif
        }
    }

#if UT_FOUND
    struct slab new_alloc_meta_slab;
    struct tcache *new_alloc_meta_tcache;
#endif

}; /** end AllocateNew decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_NEW_HPP */
