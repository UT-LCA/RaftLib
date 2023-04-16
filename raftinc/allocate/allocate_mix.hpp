/**
 * allocate_mix.hpp - An allocator uses FIFO but also support newing
 * output buffers like AllocateNew.
 * @author: Qinzhe Wu
 * @version: Tue Apr 04 10:03:06 2023
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
#ifndef RAFT_ALLOCATE_ALLOCATE_MIX_HPP
#define RAFT_ALLOCATE_ALLOCATE_MIX_HPP  1

#include <unordered_set>
#include <numeric>

#if UT_FOUND
#include <ut>
#endif

#include "raftinc/defs.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/port_info.hpp"
#include "raftinc/exceptions.hpp"
#include "raftinc/pollingworker.hpp"
#include "raftinc/oneshottask.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/allocate/mixallocmeta.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/ringbuffer.tcc"
#include "raftinc/allocate/buffer/buffertypes.hpp"

namespace raft
{

#if UT_FOUND
inline __thread tcache_perthread __perthread_mix_alloc_meta_pt;
#endif

class AllocateMix : public Allocate
{
public:

    /**
     * AllocateFIFO - base constructor
     */
    AllocateMix() : Allocate() {}

    /**
     * destructor
     */
    virtual ~AllocateMix()
    {
        for( auto *fifos : allocated_fifos )
        {
            int idx = 0;
            /* each fifos array has a terminator, nullptr */
            while( nullptr != fifos[ idx ] )
            {
                /* this deletes the fifo */
                delete fifos[ idx++ ];
            }
            /* this deletes the fifos array */
            delete[]( fifos );
        }
    }

    virtual DAG &allocate( DAG &dag )
    {
        auto func = [ & ]( PortInfo &a, PortInfo &b, void *data )
        {
            const int nfifos = std::lcm(
#if IGNORE_HINT_0CLONE
                    std::max( 1, a.my_kernel->getCloneFactor() ),
                    std::max( 1, b.my_kernel->getCloneFactor() )
#else
                    a.my_kernel->getCloneFactor(),
                    b.my_kernel->getCloneFactor()
#endif
                    );

            a.runtime_info.nfifos = b.runtime_info.nfifos = nfifos;
            auto *fifos( new FIFO*[ nfifos + 1 ] );
            /* allocate 1 more slot for the terminator, nullptr */
            allocated_fifos.insert( fifos );
            a.runtime_info.fifos = b.runtime_info.fifos = fifos;
            auto *functor( a.runtime_info.fifo_functor );
            if( nullptr != a.runtime_info.existing_buffer.ptr )
            {
                /* use existing buffer from a */
                fifos[ 0 ] = functor->make_new_fifo(
                        a.runtime_info.existing_buffer.nitems,
                        a.runtime_info.existing_buffer.start_index,
                        a.runtime_info.existing_buffer.ptr );
            }
            else
            {
                for( int i( 0 ); nfifos > i; ++i )
                {
                    fifos[ i ] = functor->make_new_fifo(
                            INITIAL_ALLOC_SIZE, ALLOC_ALIGN_WIDTH, nullptr );
                }
            }
            fifos[ nfifos ] = nullptr;
        };

        GraphTools::BFS( dag.getSourceKernels(), func );

        /* create per-kernel alloc_meta for repeated use by oneshot tasks */
        for( auto *k : dag.getKernels() )
        {
            k->setAllocMeta( new KernelMixAllocMeta( k ) );
        }

        /* preset consumers for each fifo to be nullptr, this is really just the
         * hook to let AllocateFIFOCV plug into the initial allocation phase */
        (this)->preset_fifo_consumers();

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
        slab_create( &mix_alloc_meta_slab, "mixallocmeta",
                     sizeof( RRWorkerMixAllocMeta ), 0 );
        mix_alloc_meta_tcache = slab_create_tcache( &mix_alloc_meta_slab, 64 );
    }

    virtual void perthreadInitialize()
    {
        if( ! Singleton::schedule()->doesOneShot() )
        {
            return;
        }
        tcache_init_perthread( streaming_data_tcache,
                               &__perthread_streaming_data_pt );
        tcache_init_perthread( mix_alloc_meta_tcache,
                               &__perthread_mix_alloc_meta_pt );
    }
#endif

    virtual bool dataInReady( Task *task, const port_key_t &name )
    {
        assert( ONE_SHOT != task->type );

        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );

        return tmeta->hasInputData( name );
    }

    virtual bool bufOutReady( Task *task, const port_key_t &name )
    {
        return true;
    }

    virtual void taskInit( Task *task, bool alloc_input )
    {
        if( ONE_SHOT == task->type )
        {
            auto *t( static_cast< OneShotTask* >( task ) );
            oneshot_init( t, alloc_input );
        }
        else /* if( POLLING_WORKER == task->type ||
                    CONDVAR_WORKER == task->type ) */
        {
            auto *t( static_cast< PollingWorker* >( task ) );
            polling_worker_init( t );
        }
    }

    virtual void registerConsumer( Task *task )
    {
        UNUSED( task );
    }

    virtual void taskCommit( Task *task )
    {
        if( ONE_SHOT == task->type )
        {
            oneshot_commit( static_cast< OneShotTask* >( task ) );
        }
    }

    virtual void invalidateOutputs( Task *task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
            tmeta->invalidateOutputs();
        }
        //TODO: design for oneshot task
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
            return tmeta->hasValidInput();
        }
        //TODO: design for oneshot task
        return true;
    }

    virtual int select( Task *task, const port_key_t &name, bool is_in )
    {
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        if( is_in )
        {
            return tmeta->selectIn( name );
        }
        else
        {
            return tmeta->selectOut( name );
        }
    }

    virtual void taskPop( Task *task, int selected, DataRef &item )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo, selected );
        functor->pop( fifo, item );
    }

    virtual DataRef taskPeek( Task *task, int selected )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo, selected );
        return functor->peek( fifo );
    }

    virtual void taskRecycle( Task *task, int selected )
    {
        if( ONE_SHOT != task->type )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
            tmeta->getPairIn( functor, fifo, selected );
            return functor->recycle( fifo );
        }
        // else do nothing, b/c we have dedicated the data for this task
    }

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        if( tmeta->getPairOut( functor, fifo, selected ) )
        {
            functor->push( fifo, item );
            // wake up the worker waiting for data
            (this)->wakeup_consumer( tmeta, selected );
            tmeta->nextFIFO( selected );
        }
        else
        {
            tmeta->pushOutBuf( item, selected );
        }
    }

    virtual DataRef taskAllocate( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        if( tmeta->getPairOut( functor, fifo, selected ) )
        {
            return functor->allocate( fifo );
        }
        else
        {
            return tmeta->allocateOutBuf( selected );
        }
    }

    virtual void taskSend( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< MixAllocMeta* >( task->alloc_meta ) );
        if( tmeta->getPairOut( functor, fifo, selected ) )
        {
            functor->send( fifo );
            // wake up the worker waiting for data
            (this)->wakeup_consumer( tmeta, selected );
            tmeta->nextFIFO( selected );
        }
    }

    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           int *selected, bool *is_last )
    {
        UNUSED( selected );
        auto *tmeta(
                static_cast< MixAllocMeta* >( task->alloc_meta ) );
        return tmeta->popOutBuf( pi_ptr, ref, is_last );
    }

protected:

    static inline FIFOFunctor* get_FIFOFunctor( const PortInfo &pi )
    {
        return pi.runtime_info.fifo_functor;
    }

    inline void polling_worker_init( PollingWorker *worker )
    {
        auto *kmeta( static_cast< KernelMixAllocMeta* >(
                    worker->kernel->getAllocMeta() ) );

#if USE_UT
        auto *tmeta_ptr_tmp( tcache_alloc( &__perthread_mix_alloc_meta_pt ) );
        worker->alloc_meta =
            new ( tmeta_ptr_tmp ) RRWorkerMixAllocMeta( *kmeta,
                                                        worker->clone_id );
#else
        worker->alloc_meta = new RRWorkerMixAllocMeta( *kmeta,
                                                       worker->clone_id );
#endif
    }

    inline void oneshot_init( OneShotTask *oneshot, bool alloc_input )
    {
        auto *kmeta( static_cast< KernelMixAllocMeta* >(
                    oneshot->kernel->getAllocMeta() ) );
        oneshot->stream_in = nullptr;
#if USE_UT
        auto *stream_out_ptr_tmp(
                tcache_alloc( &__perthread_streaming_data_pt ) );
        oneshot->stream_out = new ( stream_out_ptr_tmp ) StreamingData(
                oneshot, StreamingData::SINGLE_OUT );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN :
                             StreamingData::SINGLE_IN );
            auto *stream_in_ptr_tmp(
                    tcache_alloc( &__perthread_streaming_data_pt ) );
            oneshot->stream_in = new ( stream_in_ptr_tmp ) StreamingData(
                    oneshot, sd_in_type );
        }
        auto *tmeta_ptr_tmp( tcache_alloc( &__perthread_mix_alloc_meta_pt ) );
        oneshot->alloc_meta =
            new ( tmeta_ptr_tmp ) OneShotMixAllocMeta( *kmeta );
#else
        oneshot->stream_out =
            new StreamingData( oneshot, StreamingData::SINGLE_OUT );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN_1PIECE :
                             StreamingData::SINGLE_IN_1PIECE );
            oneshot->stream_in = new StreamingData( oneshot, sd_in_type );
        }

        oneshot->alloc_meta = new OneShotMixAllocMeta( *kmeta );
#endif
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
            oneshot->stream_in = nullptr;
        }
        /* stream_out might have been assigned to a consumer task as stream_in
         * if is1Piece() && isSingle() && isSent() */
        if( nullptr != oneshot->stream_out )
        {
#if USE_UT
            tcache_free( &__perthread_streaming_data_pt, oneshot->stream_out );
#else
            delete oneshot->stream_out;
#endif
            oneshot->stream_out = nullptr;
        }
#if USE_UT
        tcache_free( &__perthread_mix_alloc_meta_pt, oneshot->alloc_meta );
#else
        delete oneshot->alloc_meta;
#endif
        oneshot->alloc_meta = nullptr;
    }

    /**
     * preset_fifo_consumers - this is the hook function to allow
     * AllocateFIFOCV to override and prepare its structure during the initial
     * allocation phase
     */
    virtual void preset_fifo_consumers()
    {
        /* do nothing */
    }

    /**
     * wakeup_consumer -
     */
    virtual void wakeup_consumer( MixAllocMeta *tmeta, int selected )
    {
        /* do nothing */
        UNUSED( tmeta );
        UNUSED( selected );
    }


    /**
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_set< FIFO** > allocated_fifos;

#if UT_FOUND
    struct slab mix_alloc_meta_slab;
    struct tcache *mix_alloc_meta_tcache;
#endif

}; /** end AllocateMix decl **/

/**
 * AllocateMixCV - a special allocate that is almost identical to the basic
 * Mix allocator (i.e., AllocateMix) in most part, except this one implements
 * the interface to register and wakeup FIFO consumers.
 * Note: If not used with ScheduleMixCV, it should does everything the same as
 * AllocateMix.
 */
class AllocateMixCV : public AllocateMix
{
public:

    AllocateMixCV() : AllocateMix() {}

    virtual ~AllocateMixCV() = default;

    virtual void registerConsumer( Task *task )
    {
        assert( CONDVAR_WORKER == task->type );
        auto *t( static_cast< CondVarWorker* >( task ) );
        auto *tmeta( static_cast< RRWorkerMixAllocMeta* >(
                    task->alloc_meta ) );

        tmeta->consumerInit( t, fifo_consumers );
    }

protected:

    virtual void preset_fifo_consumers()
    {
        for( auto *fifos : allocated_fifos )
        {
            int idx = 0;
            while( nullptr != fifos[ idx ] )
            {
                /* allocate the slot earlier to avoid resize */
                fifo_consumers[ fifos[ idx++ ] ] = nullptr;
            }
        }
    }

    virtual void wakeup_consumer( MixAllocMeta *tmeta, int selected )
    {
        auto *fifo( tmeta->wakeupConsumer( selected ) );
        if( nullptr != fifo )
        {
            auto *worker( fifo_consumers[ fifo ] );
            if( nullptr != worker )
            {
                tmeta->setConsumer( selected, worker );
            }
        }
    }

private:

    std::unordered_map< FIFO*, CondVarWorker* > fifo_consumers;

}; /** end AllocateMixCV decl **/


} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
