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
    typedef RRWorkerMixAllocMeta WorkerMixAllocMeta;
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
#if ARMQ_DUMP_FIFO_STATS
                Buffer::Blocked rstat, wstat;
                fifos[ idx ]->get_zero_read_stats( rstat );
                fifos[ idx ]->get_zero_write_stats( wstat );
                std::cout << idx << " 0x" << std::hex <<
                    (uint64_t)fifos[ idx ] << std::dec << " " <<
                    rstat.bec.blocked << " " << rstat.bec.count << " " <<
                    wstat.bec.blocked << " " << wstat.bec.count << std::endl;
#endif
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
#if ARMQ_NO_HINT_0CLONE
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
#if ARMQ_DYNAMIC_ALLOC
                    fifos[ i ] = functor->make_new_fifo_linked(
                            INITIAL_ALLOC_SIZE, ALLOC_ALIGN_WIDTH, nullptr );
#else
                    fifos[ i ] = functor->make_new_fifo(
                            INITIAL_ALLOC_SIZE, ALLOC_ALIGN_WIDTH, nullptr );
#endif
                }
            }
            fifos[ nfifos ] = nullptr;
        };

        GraphTools::BFS( dag.getSourceKernels(), func );

        /* create per-kernel alloc_meta for repeated use by oneshot tasks */
        for( auto *k : dag.getKernels() )
        {
            k->setAllocMeta( new KernelFIFOAllocMeta( k ) );
        }

        /* preset consumers for each fifo to be nullptr, this is really just the
         * hook to let AllocateFIFOCV plug into the initial allocation phase */
        (this)->preset_fifo_waiters();

        (this)->ready = true;
        return dag;
    }

#if UT_FOUND
    virtual void globalInitialize() override
    {
        slab_create( &streaming_data_slab, "streamingdata",
                     sizeof( StreamingData ), 0 );
        streaming_data_tcache = slab_create_tcache( &streaming_data_slab, 64 );
        slab_create( &mix_alloc_meta_slab, "mixallocmeta",
                     sizeof( OneShotMixAllocMeta ), 0 );
        mix_alloc_meta_tcache = slab_create_tcache( &mix_alloc_meta_slab, 64 );
    }

    virtual void perthreadInitialize() override
    {
        tcache_init_perthread( streaming_data_tcache,
                               &__perthread_streaming_data_pt );
        tcache_init_perthread( mix_alloc_meta_tcache,
                               &__perthread_mix_alloc_meta_pt );
    }
#endif

    virtual bool dataInReady( Task *task, const port_key_t &name )
    {
        assert( ONE_SHOT != task->type );

        auto *tmeta( cast_worker_meta( task->alloc_meta ) );

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

    virtual void registerWaiter( Task *task )
    {
        UNUSED( task );
    }

    virtual void taskCommit( Task *task )
    {
        if( ONE_SHOT == task->type )
        {
            oneshot_commit( static_cast< OneShotTask* >( task ) );
        }
        else
        {
            worker_commit( task );
        }
    }

    virtual void invalidateOutputs( Task *task )
    {
        if( ONE_SHOT == task->type )
        {
            return;
        }
        auto *tmeta( cast_worker_meta( task->alloc_meta ) );
        tmeta->invalidateOutputs();
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        assert( ONE_SHOT != task->type );
        auto *tmeta( cast_worker_meta( task->alloc_meta ) );
        return tmeta->hasValidInput();
    }

    virtual int select( Task *task, const port_key_t &name, bool is_in )
    {
        if( ONE_SHOT != task->type )
        {
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            return is_in ? tmeta->selectIn( name ) : tmeta->selectOut( name );
        }
        else
        {
            auto *tmeta( cast_oneshot_meta( task->alloc_meta ) );
            return is_in ? tmeta->selectIn( name ) : tmeta->selectOut( name );
        }
    }

    virtual void taskPop( Task *task, int selected, DataRef &item )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_worker_meta( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo );
        functor->pop( fifo, item );
    }

    virtual DataRef taskPeek( Task *task, int selected )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        UNUSED( selected );
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_worker_meta( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo );
        return functor->peek( fifo );
    }

    virtual void taskRecycle( Task *task, int selected )
    {
        UNUSED( selected );
        if( ONE_SHOT != task->type )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            tmeta->getPairIn( functor, fifo );
            return functor->recycle( fifo );
        }
        // else do nothing, b/c we have dedicated the data for this task
    }

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        if( ONE_SHOT == task->type )
        {
            auto *tmeta( cast_oneshot_meta( task->alloc_meta ) );
            tmeta->pushOutBuf( item, selected );
        }
        else
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            if( tmeta->getPairOut( functor, fifo ) )
            {
                functor->push( fifo, item );
                tmeta->nextFIFO( selected );
            }
            else
            {
                tmeta->pushOutBuf( item, selected );
#if ! ARMQ_NO_HINT && ARMQ_DUMP_FIFO_STATS
                tmeta->oneshotCnt();
#endif
                tmeta->nextFIFO( selected );
            }
        }
    }

    virtual DataRef taskAllocate( Task *task, int selected )
    {
        if( ONE_SHOT == task->type )
        {
            auto *tmeta( cast_oneshot_meta( task->alloc_meta ) );
            return tmeta->allocateOutBuf( selected );
        }
        else
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            if( tmeta->getPairOut( functor, fifo ) )
            {
                return functor->allocate( fifo );
            }
            else
            {
#if ! ARMQ_NO_HINT && ARMQ_DUMP_FIFO_STATS
                tmeta->oneshotCnt();
#endif
                tmeta->nextFIFO( selected );
                return tmeta->allocateOutBuf( selected );
            }
        }
    }

    virtual void taskSend( Task *task, int selected )
    {
        if( ONE_SHOT != task->type )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            if( tmeta->getPairOut( functor, fifo ) )
            {
                functor->send( fifo );
                tmeta->nextFIFO( selected );
            }
        }
    }

    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           int *selected, bool *is_last )
    {
        UNUSED( selected );
        if( ONE_SHOT == task->type )
        {
            auto *tmeta( cast_oneshot_meta( task->alloc_meta ) );
            return tmeta->popOutBuf( pi_ptr, ref, is_last );
        }
        else
        {
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            return tmeta->popOutBuf( pi_ptr, ref, is_last );
        }
    }

protected:

    inline static WorkerMixAllocMeta *cast_worker_meta( TaskAllocMeta *meta )
    {
        return static_cast< WorkerMixAllocMeta* >( meta );
    }

    inline static OneShotMixAllocMeta *cast_oneshot_meta( TaskAllocMeta *meta )
    {
        return static_cast< OneShotMixAllocMeta* >( meta );
    }

    static inline FIFOFunctor* get_FIFOFunctor( const PortInfo &pi )
    {
        return pi.runtime_info.fifo_functor;
    }

    static inline void polling_worker_init( PollingWorker *worker )
    {
        auto *kmeta( static_cast< KernelFIFOAllocMeta* >(
                    worker->kernel->getAllocMeta() ) );
        worker->alloc_meta = new WorkerMixAllocMeta( *kmeta,
                                                     worker->clone_id );
    }

    __attribute__((noinline)) /* function accessing tls cannot inline */
    void oneshot_init( OneShotTask *oneshot, bool alloc_input )
    {
        auto *kmeta( static_cast< KernelFIFOAllocMeta* >(
                    oneshot->kernel->getAllocMeta() ) );
        oneshot->stream_in = nullptr;
#if USE_UT
        preempt_disable();
        auto *stream_out_ptr_tmp(
                tcache_alloc( &__perthread_streaming_data_pt ) );
        auto *tmeta_ptr_tmp( tcache_alloc( &__perthread_mix_alloc_meta_pt ) );
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
        preempt_enable();
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

    static __attribute__((noinline)) /* function accessing tls cannot inline */
    void oneshot_commit( OneShotTask *oneshot )
    {
#if USE_UT
        preempt_disable();
#endif
        if( nullptr != oneshot->stream_in )
        {
#if USE_UT
            oneshot->stream_in->~StreamingData();
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
            oneshot->stream_out->~StreamingData();
            tcache_free( &__perthread_streaming_data_pt, oneshot->stream_out );
#else
            delete oneshot->stream_out;
#endif
            oneshot->stream_out = nullptr;
        }
#if USE_UT
        tcache_free( &__perthread_mix_alloc_meta_pt,
                     cast_oneshot_meta( oneshot->alloc_meta ) );
        preempt_enable();
#else
        delete oneshot->alloc_meta;
#endif
        oneshot->alloc_meta = nullptr;
    }

    static inline void worker_commit( Task *task )
    {
        delete task->alloc_meta;
    }

    /**
     * preset_fifo_waiters - this is the hook function to allow
     * AllocateFIFOCV to override and prepare its structure during the initial
     * allocation phase
     */
    virtual void preset_fifo_waiters()
    {
        /* do nothing */
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

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        if( ONE_SHOT == task->type )
        {
            auto *tmeta( cast_oneshot_meta( task->alloc_meta ) );
            tmeta->pushOutBuf( item, selected );
        }
        else
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            if( tmeta->getPairOut( functor, fifo ) )
            {
                functor->push( fifo, item );
                // wake up the worker waiting for data
                tmeta->wakeupConsumer();
                tmeta->nextFIFO( selected );
            }
            else
            {
                tmeta->pushOutBuf( item, selected );
            }
        }
    }

    virtual void taskSend( Task *task, int selected )
    {
        if( ONE_SHOT != task->type )
        {
            FIFOFunctor *functor;
            FIFO *fifo;
            auto *tmeta( cast_worker_meta( task->alloc_meta ) );
            if( tmeta->getPairOut( functor, fifo ) )
            {
                functor->send( fifo );
                // wake up the worker waiting for data
                tmeta->wakeupConsumer();
                tmeta->nextFIFO( selected );
            }
        }
    }

    virtual void registerWaiter( Task *task )
    {
        assert( CONDVAR_WORKER == task->type );
        auto *t( static_cast< CondVarWorker* >( task ) );
        auto *tmeta( static_cast< RRWorkerMixAllocMeta* >(
                    task->alloc_meta ) );

        tmeta->waiterInit( t, fifo_waiters );
    }

protected:

    virtual void preset_fifo_waiters()
    {
        for( auto *fifos : allocated_fifos )
        {
            int idx = 0;
            while( nullptr != fifos[ idx ] )
            {
                /* allocate the slot earlier to avoid resize */
                fifo_waiters[ fifos[ idx++ ] ] = { nullptr, nullptr };
            }
        }
    }

private:

    fifo_waiter_map_t fifo_waiters;

}; /** end AllocateMixCV decl **/


} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
