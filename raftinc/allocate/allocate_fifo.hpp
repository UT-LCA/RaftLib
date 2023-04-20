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
                    std::max( 1, a.my_kernel->getCloneFactor() ),
                    std::max( 1, b.my_kernel->getCloneFactor() ) );
            a.runtime_info.nfifos = b.runtime_info.nfifos = nfifos;
            auto *fifos( new FIFO*[ nfifos + 1 ] );
            /* allocate 1 more for nullptr */
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
            /* mark the end of the fifos array */
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
        (this)->preset_fifo_consumers();

        (this)->ready = true;
        return dag;
    }

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
        UNUSED( alloc_input );
        worker_init( task );
    }

    virtual void registerConsumer( Task *task )
    {
        UNUSED( task );
    }

    virtual void taskCommit( Task *task )
    {
        UNUSED( task );
    }

    virtual void invalidateOutputs( Task *task )
    {
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->invalidateOutputs();
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        auto *tmeta( cast_meta( task->alloc_meta ) );
        return tmeta->hasValidInput();
    }

    virtual int select( Task *task, const port_key_t &name, bool is_in )
    {
        auto *tmeta( cast_meta( task->alloc_meta ) );
        return is_in ? tmeta->selectIn( name ) : tmeta->selectOut( name );
    }

    virtual void taskPop( Task *task, int selected, DataRef &item )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo );
        functor->pop( fifo, item );
    }

    virtual DataRef taskPeek( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo );
        return functor->peek( fifo );
    }

    virtual void taskRecycle( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairIn( functor, fifo );
        return functor->recycle( fifo );
    }

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairOut( functor, fifo );
        functor->push( fifo, item );
        tmeta->nextFIFO( selected );
    }

    virtual DataRef taskAllocate( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairOut( functor, fifo );
        return functor->allocate( fifo );
    }

    virtual void taskSend( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairOut( functor, fifo );
        functor->send( fifo );
        tmeta->nextFIFO( selected );
    }

    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           int *selected, bool *is_last )
    {
        throw MethodNotImplementdException( "AllocateFIFO::schedPop()" );
        UNUSED( task );
        UNUSED( pi_ptr );
        UNUSED( ref );
        UNUSED( selected );
        UNUSED( is_last );
        return true;
    }

protected:

#if RAFT_GREEDY_FIFO_SELECTION
    typedef GreedyTaskFIFOAllocMeta WorkerFIFOAllocMeta;
#else
    typedef RRTaskFIFOAllocMeta WorkerFIFOAllocMeta;
#endif

    inline static WorkerFIFOAllocMeta *cast_meta( TaskAllocMeta *meta )
    {
        return static_cast< WorkerFIFOAllocMeta* >( meta );
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
        auto *tmeta( cast_meta( task->alloc_meta ) );

        return tmeta->hasInputData( name );
    }

    inline void worker_init( Task *task )
    {
        auto *worker( static_cast< PollingWorker* >( task ) );
        auto *kmeta( static_cast< KernelFIFOAllocMeta* >(
                    task->kernel->getAllocMeta() ) );
        task->alloc_meta =
            new WorkerFIFOAllocMeta( *kmeta, worker->clone_id );
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
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_set< FIFO** > allocated_fifos;

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

    virtual void taskPush( Task *task, int selected, DataRef &item )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairOut( functor, fifo );
        functor->push( fifo, item );
        // wake up the worker waiting for data
        tmeta->wakeupConsumer();
        tmeta->nextFIFO( selected );
    }

    virtual void taskSend( Task *task, int selected )
    {
        FIFOFunctor *functor;
        FIFO *fifo;
        auto *tmeta( cast_meta( task->alloc_meta ) );
        tmeta->getPairOut( functor, fifo );
        functor->send( fifo );
        // wake up the worker waiting for data
        tmeta->wakeupConsumer();
        tmeta->nextFIFO( selected );
    }

    virtual void registerConsumer( Task *task )
    {
        assert( CONDVAR_WORKER == task->type );
        auto *tmeta( cast_meta( task->alloc_meta ) );
        auto *worker( static_cast< CondVarWorker* >( task ) );
        tmeta->consumerInit( worker, fifo_consumers );
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

    std::unordered_map< FIFO*, CondVarWorker* > fifo_consumers;

}; /** end AllocateFIFOCV decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
