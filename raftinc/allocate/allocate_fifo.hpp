/**
 * allocate_fifo.hpp - fifo-base allocate class. This object has
 * several useful features and data structures, namely the set
 * of all source kernels and all the kernels within the graph.
 * There is also a list of all the currently allocated FIFO
 * objects within the streaming graph. This is primarily for
 * instrumentation puposes.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Sep 16 20:20:06 2014
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
#ifndef RAFT_ALLOCATE_ALLOCATE_FIFO_HPP
#define RAFT_ALLOCATE_ALLOCATE_FIFO_HPP  1

#include <numeric>
#include <unordered_set>
#include <unordered_map>

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
#include "raftinc/allocate/fifoallocmeta.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/ringbuffer.tcc"
#include "raftinc/allocate/buffer/buffertypes.hpp"

/**
 * ALLOC_ALIGN_WIDTH - in previous versions we'd align based
 * on perceived vector width, however, there's more benefit
 * in aligning to cache line sizes.
 */

#if defined __AVX__ || __AVX2__ || _WIN64
#define ALLOC_ALIGN_WIDTH L1D_CACHE_LINE_SIZE
#else
#define ALLOC_ALIGN_WIDTH L1D_CACHE_LINE_SIZE
#endif

#define INITIAL_ALLOC_SIZE 64

namespace raft
{

class AllocateFIFO : public Allocate
{
public:

    /**
     * AllocateFIFO - base constructor
     */
    AllocateFIFO() : Allocate() {}

    /**
     * destructor
     */
    virtual ~AllocateFIFO()
    {
        for( auto *fifo : allocated_fifos )
        {
            delete( fifo );
        }
    }

    virtual DAG &allocate( DAG &dag )
    {
        auto func = [ & ]( PortInfo &a, PortInfo &b, void *data )
        {
            const int nfifos = std::lcm( a.my_kernel->getCloneFactor(),
                                         b.my_kernel->getCloneFactor() );
            a.runtime_info.nfifos = b.runtime_info.nfifos = nfifos;
            auto *fifos( new FIFO*[ nfifos + 1 ] ); /* TODO: free the array? */
            /* allocate 1 more FIFO for oneshot tasks */
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
            /* allocate one more fifo as the mutex-protected fifo */
            fifos[ nfifos ] = functor->make_new_fifo_mutexed(
                    INITIAL_ALLOC_SIZE, ALLOC_ALIGN_WIDTH, nullptr );
        };

        GraphTools::BFS( dag.getSourceKernels(), func );

        /* create per-kernel alloc_meta for repeated use by oneshot tasks */
        for( auto *k : dag.getKernels() )
        {
            auto *tmeta( new KernelFIFOAllocMeta(
                        k->input.size(), k->output.size() ) );
            k->setAllocMeta( tmeta );

            int idx = 0;
            for( auto &p : k->input )
            {
                tmeta->name2port_in.emplace( p.first, idx );
                tmeta->ports_in_info[ idx++ ] = &p.second;
            }
            idx = 0;
            for( auto &p : k->output )
            {
                tmeta->name2port_out.emplace( p.first, idx );
                tmeta->ports_out_info[ idx++ ] = &p.second;
            }
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
        streaming_data_tcache = slab_create_tcache( &streaming_data_slab,
                                                    TCACHE_DEFAULT_MAG_SIZE );
    }

    virtual void perthreadInitialize()
    {
        if( ! Singleton::schedule()->doesOneShot() )
        {
            return;
        }
        tcache_init_perthread( streaming_data_tcache,
                               &__perthread_streaming_data_pt );
    }
#endif

    virtual bool dataInReady( Task *task, const port_key_t &name )
    {
        return task_has_input_data( task );
    }

    virtual bool bufOutReady( Task *task, const port_key_t &name )
    {
        return true;
    }

    virtual void taskInit( Task *task, bool alloc_input )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( static_cast< PollingWorker* >( task ) );
            polling_worker_init( t );
        }
        else if( CONDVAR_WORKER == task->type )
        {
            auto *t( static_cast< CondVarWorker* >( task ) );
            condvar_worker_init( t );
        }
        else if( ONE_SHOT == task->type )
        {
            auto *t( static_cast< OneShotTask* >( task ) );
            oneshot_init( t, alloc_input );
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
            auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                        task->alloc_meta ) );
            tmeta->invalidateOutputs();
        }
        //TODO: design for oneshot task
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                        task->alloc_meta ) );
            return tmeta->hasValidInput();
        }
        //TODO: design for oneshot task
        return true;
    }

    virtual int select( Task *task, const port_key_t &name, bool is_in )
    {
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
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
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        tmeta->getPairIn( &functor, &fifo, selected );
        functor->pop( fifo, item );
    }

    virtual DataRef taskPeek( Task *task, int selected )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        tmeta->getPairIn( &functor, &fifo, selected );
        return functor->peek( fifo );
    }

    virtual void taskRecycle( Task *task, int selected )
    {
        if( ONE_SHOT != task->type )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                        task->alloc_meta ) );
            tmeta->getPairIn( &functor, &fifo, selected );
            return functor->recycle( fifo );
        }
        // else do nothing, b/c we have dedicated the data for this task
    }

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        tmeta->getPairOut( &functor, &fifo, selected, ONE_SHOT == task->type );
        functor->push( fifo, item );
        // wake up the worker waiting for data
        (this)->wakeup_consumer( tmeta, selected );
        tmeta->nextFIFO( selected );
    }

    virtual DataRef taskAllocate( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        tmeta->getPairOut( &functor, &fifo, selected, ONE_SHOT == task->type );
        return functor->allocate( fifo );
    }

    virtual void taskSend( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        tmeta->getPairOut( &functor, &fifo, selected, ONE_SHOT == task->type );
        functor->send( fifo );
        // wake up the worker waiting for data
        (this)->wakeup_consumer( tmeta, selected );
        tmeta->nextFIFO( selected );
    }

    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           int *selected, bool *is_last )
    {
        assert( ONE_SHOT == task->type );
        UNUSED( is_last );
        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );
        while ( *selected < task->kernel->output.size() )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            tmeta->getDrainPairOut( &functor, &fifo, *selected );
            if( 0 >= fifo->size() )
            {
                *selected++;
                continue;
            }
            ref = functor->oneshot_allocate();
            /* NOTE: might have race condition, fifo was not empty but it got
             * popped by another oneshot task doing schedPop then blocking */
            functor->pop( fifo, ref );
            pi_ptr = tmeta->getPortsOutInfo()[ *selected ];
            return true;
        }
        return false;
    }

protected:

    static inline FIFOFunctor* get_FIFOFunctor( const PortInfo &pi )
    {
        return pi.runtime_info.fifo_functor;
    }

    /**
     * task_has_input_data - check each input fifos for available
     * data, returns true if any of the input fifos has available
     * data.
     * @param kernel - raft::Task*
     * @param name - raft::port_key_t &
     * @return bool  - true if input data available.
     */
    bool task_has_input_data( Task *task,
                              const port_key_t &name = null_port_value )
    {
        assert( ONE_SHOT != task->type );

        auto *tmeta( static_cast< FIFOAllocMetaInterface* >(
                    task->alloc_meta ) );

        return tmeta->hasInputData( name );
    }

    inline void polling_worker_init( PollingWorker *worker )
    {
        auto *kmeta( static_cast< KernelFIFOAllocMeta* >(
                    worker->kernel->getAllocMeta() ) );

        if( FIFOAllocMetaInterface::qualifyStatic( worker->kernel ) )
        {
            worker->alloc_meta = kmeta;
        }
        else
        {
            worker->alloc_meta = new RRTaskFIFOAllocMeta( *kmeta,
                                                          worker->clone_id );
        }
    }

    inline void condvar_worker_init( CondVarWorker *worker )
    {
        auto *kmeta( static_cast< KernelFIFOAllocMeta* >(
                    worker->kernel->getAllocMeta() ) );

        worker->alloc_meta =
            new RRTaskFIFOAllocMeta( *kmeta, worker->clone_id );
    }

    inline void oneshot_init( OneShotTask *oneshot, bool alloc_input )
    {
        oneshot->stream_in = nullptr;
        auto sd_type( ( 1 < oneshot->kernel->output.size() ) ?
                      StreamingData::OUT_1PIECE :
                      StreamingData::SINGLE_OUT_1PIECE );
#if USE_UT
        auto *stream_out_ptr_tmp(
                tcache_alloc( &__perthread_streaming_data_pt ) );
        oneshot->stream_out =
            new ( stream_out_ptr_tmp ) StreamingData( oneshot, sd_type );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN_1PIECE :
                             StreamingData::SINGLE_IN_1PIECE );
            auto *stream_in_ptr_tmp(
                    tcache_alloc( &__perthread_streaming_data_pt ) );
            oneshot->stream_in = new ( stream_in_ptr_tmp ) StreamingData(
                    oneshot, sd_in_type );
        }
#else
        oneshot->stream_out = new StreamingData( oneshot, sd_type );
        if( alloc_input )
        {
            auto sd_in_type( 1 < oneshot->kernel->input.size() ?
                             StreamingData::IN_1PIECE :
                             StreamingData::SINGLE_IN_1PIECE );
            oneshot->stream_in = new StreamingData( oneshot, sd_in_type );
        }
#endif
        auto &output_ports( oneshot->kernel->output );

        oneshot->alloc_meta = oneshot->kernel->getAllocMeta();

        for( auto &p : output_ports )
        {
            oneshot->stream_out->set(
                    p.first, get_FIFOFunctor( p.second )->oneshot_allocate() );
            /* TODO: needs to find a place to release the malloced data */
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
     * wakeup_consumer - this is the hook function to allow
     * AllocateFIFOCV to override and wakeup the consumer based on its record
     */
    virtual void wakeup_consumer( FIFOAllocMetaInterface *tmeta, int selected )
    {
        /* do nothing */
        UNUSED( tmeta );
        UNUSED( selected );
    }

    /**
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_set< FIFO* > allocated_fifos;

}; /** end AllocateFIFO decl **/

/**
 * AllocateFIFOCV - a special allocate that is almost identical to the basic
 * FIFO allocator (i.e., AllocateFIFO) in most part, except this one implements
 * the interface to register and wakeup FIFO consumers.
 * Note: If not used with ScheduleCV, it should does everything the same as
 * AllocateFIFO.
 */
class AllocateFIFOCV : public AllocateFIFO
{
public:

    AllocateFIFOCV() : AllocateFIFO() {}

    virtual ~AllocateFIFOCV() = default;

    virtual void registerConsumer( Task *task )
    {
        assert( CONDVAR_WORKER == task->type );
        auto *t( static_cast< CondVarWorker* >( task ) );
        condvar_worker_register_consumer( t );
    }

protected:

    virtual void preset_fifo_consumers()
    {
        for( auto *fifo : allocated_fifos )
        {
            /* allocate the slot earlier to avoid resize */
            fifo_consumers[ fifo ] = nullptr;
        }
    }

    virtual void wakeup_consumer( FIFOAllocMetaInterface *tmeta, int selected )
    {
        auto *fifo( tmeta->wakeupConsumer( selected ) );
        if( nullptr != fifo )
        {
            tmeta->setConsumer( selected, fifo_consumers[ fifo ] );
        }
    }

private:
    inline void condvar_worker_register_consumer( CondVarWorker *worker )
    {
        auto *tmeta( static_cast< RRTaskFIFOAllocMeta* >(
                    worker->alloc_meta ) );

        auto ninputs( tmeta->ninputs );
        for( auto i( 0 ); ninputs > i; ++i )
        {
            auto *pi( tmeta->ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            for( int j( 0 ); nfifos > j; ++j )
            {
                fifo_consumers[ fifos[ j ] ] = worker;
            }
        }

        tmeta->newConsumersArr();
    }

    std::unordered_map< FIFO*, CondVarWorker* > fifo_consumers;

}; /** end AllocateFIFOCV decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
