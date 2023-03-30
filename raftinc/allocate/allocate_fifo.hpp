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
#include "raftinc/pollingworker.hpp"
#include "raftinc/oneshottask.hpp"
#include "raftinc/allocate/allocate.hpp"
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

#if UT_FOUND
inline __thread tcache_perthread __perthread_streaming_data_pt;
#endif

struct TaskFIFOPort
{
    FIFO **fifos;
    FIFOFunctor *functor;
    const int nfifos;
    int idx;
    TaskSchedMeta **tasks;
    bool readonly = false;
    /* when in Kernel::AllocMeta, there are multi-readers but avoid writing */
    TaskFIFOPort( int n ) : nfifos( n )
    {
        fifos = new FIFO*[ n ];
        tasks = new TaskSchedMeta*[ n ](); // serve as the cache of fifo_tmeta
    }
    /* define the following constructor to allow unordered_map index access */
    TaskFIFOPort() : nfifos( 0 )
    {
        throw MethodNotImplementdException( "TaskFIFOPort()" );
    }
    TaskFIFOPort( TaskFIFOPort &&other ) :
        fifos( other.fifos ), functor( other.functor ), nfifos( other.nfifos ),
        idx( other.idx )
    {
        other.fifos = nullptr;
    }
    TaskFIFOPort( const TaskFIFOPort &other ) = delete;
    virtual ~TaskFIFOPort()
    {
        if( nullptr != fifos )
        {
            delete[] fifos;
        }
        if( nullptr != tasks )
        {
            delete[] tasks;
        }
    }
    virtual bool wakupConsumer()
    {
        if( readonly )
        {
            return true;
        }
        // wake up the worker waiting for data
        if( nullptr != tasks[ idx ] )
        {
            tasks[ idx ]->wakeup();
            return true;
        }
        return false;
        /* let AllocateFIFO knows that it should looking up fifo_tmeta */
    }
    virtual void nextFIFO()
    {
        if( readonly )
        {
            return;
        }
        // select next fifo in Round-Robin manner
        idx = ( idx + 1 ) % nfifos;
    }
};

struct TaskFIFOAllocMeta : public TaskAllocMeta
{
    TaskFIFOAllocMeta() :
        TaskAllocMeta(), selected_in( nullptr ), selected_out( nullptr ) {}
    virtual ~TaskFIFOAllocMeta() = default;

    std::unordered_map< port_key_t, TaskFIFOPort* > name2port_in;
    std::unordered_map< port_key_t, TaskFIFOPort* > name2port_out;

    /* use vector for faster iterating */
    std::vector< TaskFIFOPort > ports_in;
    std::vector< TaskFIFOPort > ports_out;

    TaskFIFOPort *selected_in;
    TaskFIFOPort *selected_out;
};

class AllocateFIFO : public Allocate
{
public:

    /**
     * AllocateFIFO - base constructor, really doesn't do too much
     * save for setting the global variables all_kernels and
     * source_kernels from the DAG object.
     * @param dag - raft::DAG&
     */
    AllocateFIFO() : Allocate() {}

    /**
     * destructor
     */
    virtual ~AllocateFIFO()
    {
        for( auto &p : port_fifo )
        {
            for( auto *fifo : *p.second )
            {
                delete( fifo );
            }
            /* clear the vector to avoid double free because port_fifo
             * is indexed by both port_info */
            p.second->clear();
        }
    }

    /**
     * isReady - call after initializing the allocate thread, check if the
     * initial allocation is complete.
     */
    bool isReady() const
    {
        return ready;
    }

    /**
     * isExited - call after initializing the allocate thread, check if the
     * allocate thread has exited.
     */
    bool isExited() const
    {
        return exited;
    }

    virtual DAG &allocate( DAG &dag )
    {
        auto func = [ & ]( PortInfo &a, PortInfo &b, void *data )
        {
            const int nfifos = std::max( a.my_kernel->getCloneFactor(),
                                         b.my_kernel->getCloneFactor() );
            auto *fifos( new std::vector< FIFO* >( nfifos + 1 ) );
            (this)->port_fifo[ &a ] = fifos;
            (this)->port_fifo[ &b ] = fifos;
            if( nullptr != a.runtime_info.existing_buffer.ptr )
            {
                /* use existing buffer from a */
                fifos->at( 0 ) = get_FIFOFunctor(
                        a )->make_new_fifo(
                            a.runtime_info.existing_buffer.nitems,
                            a.runtime_info.existing_buffer.start_index,
                            a.runtime_info.existing_buffer.ptr );
            }
            else
            {
                for( int i( 0 ); nfifos > i; ++i )
                {
                    fifos->at( i ) =
                        get_FIFOFunctor( a )->make_new_fifo(
                                INITIAL_ALLOC_SIZE,
                                ALLOC_ALIGN_WIDTH,
                                nullptr );
                    /* allocate the slot earlier to avoid fifo_tmeta resize */
                    fifo_tmeta[ fifos->at( i ) ] = nullptr;
                }
            }
            /* allocate one more fifo as the mutex-protected fifo */
            fifos->back() =
                get_FIFOFunctor(
                        a )->make_new_fifo_mutexed( INITIAL_ALLOC_SIZE,
                                                    ALLOC_ALIGN_WIDTH,
                                                    nullptr );
            /* allocate the slot earlier to avoid fifo_tmeta resize */
            fifo_tmeta[ fifos->back() ] = nullptr;
            a.runtime_info.fifo = b.runtime_info.fifo = fifos->at( 0 );
        };

        GraphTools::BFS( dag.getSourceKernels(), func );

        /* create per-kernel alloc_meta for repeated use by oneshot tasks */
        for( auto *k : dag.getKernels() )
        {
            auto *tmeta( new TaskFIFOAllocMeta() );
            k->setAllocMeta( tmeta );

            for( auto &p : k->output )
            {
                tmeta->ports_out.emplace_back( 1 );
                tmeta->name2port_out.emplace( p.first,
                                              &tmeta->ports_out.back() );
                auto &port( tmeta->ports_out.back() );
                port.readonly = true; /* avoid concurrent writes to the port */
                port.idx = 0;
                port.fifos[ 0 ] = port_fifo[ &p.second ]->back();
                port.functor = p.second.runtime_info.fifo_functor;
            }
        }

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
        return kernel_has_output_buf( task->kernel );
    }

    virtual bool getDataIn( Task *task, const port_key_t &name )
    {
        return task_has_input_data( task, name );
    }

    virtual bool getBufOut( Task *task, const port_key_t &name )
    {
        return kernel_has_output_buf( task->kernel, name );
    }

    virtual StreamingData &getDataIn( Task *task )
    {
        return kernel_pack_input_data( task->kernel );
    }

    virtual StreamingData &getBufOut( Task *task )
    {
        return task_pack_output_buf( task );
    }

    virtual void taskInit( Task *task, bool alloc_input )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( static_cast< PollingWorker* >( task ) );
            polling_worker_init( t );
        }
        else if( ONE_SHOT == task->type )
        {
            auto *t( static_cast< OneShotTask* >( task ) );
            oneshot_init( t, alloc_input );
        }
    }

    virtual void registerConsumer( Task *task )
    {
        assert( POLLING_WORKER == task->type );
        auto *t( static_cast< PollingWorker* >( task ) );
        polling_worker_register_consumer( t );
    }

    virtual void commit( Task *task )
    {
        task_commit( task );
    }

    virtual void invalidateOutputs( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( static_cast< PollingWorker* >( task ) );
            polling_worker_invalidate_outputs( t );
        }
        //TODO: design for oneshot task
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( static_cast< PollingWorker* >( task ) );
            return polling_worker_has_input_ports( t );
        }
        //TODO: design for oneshot task
        return true;
    }

    virtual void select( Task *task, const port_key_t &name, bool is_in )
    {
        //assert( task->alloc_meta != task->kernel->getAllocMeta() );
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        if( is_in )
        {
            auto iter( tmeta->name2port_in.find( name ) );
            assert( tmeta->name2port_in.end() != iter );
            tmeta->selected_in = iter->second;
        }
        else
        {
            auto iter( tmeta->name2port_out.find( name ) );
            assert( tmeta->name2port_out.end() != iter );
            tmeta->selected_out = iter->second;
        }
    }

    virtual void taskPop( Task *task, DataRef &item )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        auto *port( nullptr == tmeta->selected_in ?
                    &tmeta->ports_in[ 0 ] : tmeta->selected_in );
        port->functor->pop( port->fifos[ port->idx ], item );
    }

    virtual DataRef taskPeek( Task *task )
    {
        // oneshot task should have all input data satisfied by StreamingData
        assert( ONE_SHOT != task->type );

        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        auto *port( nullptr == tmeta->selected_in ?
                    &tmeta->ports_in[ 0 ] : tmeta->selected_in );
        return port->functor->peek( port->fifos[ port->idx ] );
    }

    virtual void taskRecycle( Task *task )
    {
        if( ONE_SHOT != task->type )
        {
            auto *tmeta(
                    static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
            auto *port( nullptr == tmeta->selected_in ?
                        &tmeta->ports_in[ 0 ] : tmeta->selected_in );
            port->functor->recycle( port->fifos[ port->idx ] );
        }
        // else do nothing, b/c we have dedicated the data for this task
    }

    virtual void taskPush( Task *task, DataRef &item )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        auto *port( nullptr == tmeta->selected_out ?
                    &tmeta->ports_out[ 0 ] : tmeta->selected_out );
        port->functor->push( port->fifos[ port->idx ], item );
        // wake up the worker waiting for data
        if( ! port->wakupConsumer() )
        {
            port->tasks[ port->idx ] = fifo_tmeta[ port->fifos[ port->idx ] ];
        }
        port->nextFIFO();
    }

    virtual DataRef taskAllocate( Task *task )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        auto *port( nullptr == tmeta->selected_out ?
                    &tmeta->ports_out[ 0 ] : tmeta->selected_out );
        return port->functor->allocate( port->fifos[ port->idx ] );
    }

    virtual void taskSend( Task *task )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        auto *port( nullptr == tmeta->selected_out ?
                    &tmeta->ports_out[ 0 ] : tmeta->selected_out );
        port->functor->send( port->fifos[ port->idx ] );
        // wake up the worker waiting for data
        if( ! port->wakupConsumer() )
        {
            port->tasks[ port->idx ] = fifo_tmeta[ port->fifos[ port->idx ] ];
        }
        port->nextFIFO();
    }

    virtual DataRef portPop( const PortInfo *pi )
    {
        auto *functor( pi->runtime_info.fifo_functor );
        FIFO *fifo = port_fifo[ pi ]->back();
        //FIXME: could have race condition, that the data is popped by another thread
        if( 0 == fifo->size() )
        {
            return DataRef();
        }
        DataRef ref( functor->oneshot_allocate() );
        functor->pop( fifo, ref );
        return ref;
    }

protected:

    static inline FIFO* get_FIFO( const PortInfo &pi )
    {
        return pi.runtime_info.fifo;
    }

    static inline FIFOFunctor* get_FIFOFunctor( const PortInfo &pi )
    {
        return pi.runtime_info.fifo_functor;
    }


    /**
     * kernel_has_input_data - check each input port for available
     * data, returns true if any of the input ports has available
     * data.
     * @param kernel - raft::Kernel*
     * @return bool  - true if input data available.
     */
    static bool kernel_has_input_data( Kernel *kernel,
                                       const port_key_t &name =
                                       null_port_value )
    {
        auto &port_list( kernel->input );
        if( 0 == port_list.size() )
        {
           /** only output ports, keep calling till exits **/
           return( true );
        }

        if( null_port_value != name )
        {
            auto &info( kernel->getInput( name ) );
            const auto size( get_FIFO( info )->size() );
            return ( size > 0 );
        }

        /**
         * NOTE: this was added as a reqeuest, need to update wiki,
         * the first hit to this one will take an extra few cycles
         * to process the jmp, however, after that, the branch
         * taken is incredibly easy and we should be able to do
         * this as if the switch statement wasn't there at all.
         * - an alternative to using the kernel variable would
         * be to implement a new subclass of kernel...that's doable
         * too but we'd have to make dependent template functions
         * that would use the type info to select the right behavior
         * which we're doing dynamically below in the switch statement.
         */
        switch( kernel->sched_trigger )
        {
            case( trigger::any_port ):
            {
                for( auto &p : port_list )
                {
                   const auto size( get_FIFO( p.second )->size() );
                   if( size > 0 )
                   {
                      return( true );
                   }
                }
            }
            break;
            case( trigger::all_port ):
            {
                for( auto &p : port_list )
                {
                   const auto size( get_FIFO( p.second )->size() );
                   /** no data avail on this port, return false **/
                   if( size == 0 )
                   {
                      return( false );
                   }
                }
                /** all ports have data, return true **/
                return( true );
            }
            break;
            default:
            {
                //TODO add exception class here
                std::cerr << "invalid scheduling behavior set, exiting!\n";
                exit( EXIT_FAILURE );
            }
        }
        /** we should have returned before here, keep compiler happy **/
        return( false );
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
        assert( POLLING_WORKER == task->type );

        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( task->alloc_meta ) );
        if( 0 == tmeta->ports_in.size() )
        {
            /** only output ports, keep calling till exits **/
            return( true );
        }

        if( null_port_value != name )
        {
            auto &port( *tmeta->name2port_in[ name ] );
            auto *fifos( port.fifos );
            auto &nfifos( port.nfifos );
            auto &idx( port.idx );
            for( int i( 0 ); nfifos > i; ++i )
            {
                const auto size( fifos[ ( idx + i ) % nfifos ]->size() );
                if( size > 0 )
                {
                    idx = ( idx + i ) % nfifos;
                    return true;
                }
            }
            return false;
        }

        for( auto &port : tmeta->ports_in )
        {
            auto *fifos( port.fifos );
            auto &nfifos( port.nfifos );
            auto &idx( port.idx );
            for( int i( 0 ); nfifos > i; ++i )
            {
                const auto size( fifos[ ( idx + i ) % nfifos ]->size() );
                if( size > 0 )
                {
                    idx = ( idx + i ) % nfifos;
                    return true;
                }
            }
        }

        return( false );
    }


    /**
     * kernel_has_output_buf - check each output port for available
     * buffer, returns true if each output port has available
     * buffer.
     * @param kernel - raft::Kernel*
     * @return bool  - true if output buffer available.
     */
    static bool kernel_has_output_buf( Kernel *kernel,
                                       const port_key_t &name =
                                       null_port_value )
    {
        auto &port_list( kernel->output );
        if( 0 == port_list.size() )
        {
           /** only output ports, keep calling till exits **/
           return( false );
        }

        if( null_port_value != name )
        {
            auto &info( kernel->getOutput( name ) );
            const auto cap( get_FIFO( info )->capacity() );
            return ( cap > 0 );
        }

        for( auto &p : port_list )
        {
           const auto cap( get_FIFO( p.second )->capacity() );
           /** no data avail on this port, return false **/
           if( cap == 0 )
           {
              return( false );
           }
        }
        /** all ports have buffer, return true **/
        return( true );
    }

    /**
     * kernel_has_no_input_ports - pretty much exactly like the
     * function name says, if the param kernel has no valid
     * input ports (this function assumes that kernelHasInputData()
     * has been called and returns false before this function
     * is called) then it returns true.
     * @params   kernel - raft::kernel*
     * @return  bool   - true if no valid input ports avail
     */
    static bool kernel_has_no_input_ports( Kernel *kernel )
    {
        auto &port_list( kernel->input );
        /** assume data check is already complete **/
        for( auto &p : port_list )
        {
            if( ! get_FIFO( p.second )->is_invalid() )
            {
                return( false );
            }
        }
        return( true );
    }


    /**
     * kernel_pack_input_data - assemble data of input port.
     * @param kernel - raft::Kernel*
     * @return StreamingData.
     */
    static StreamingData &kernel_pack_input_data( Kernel *kernel )
    {
        auto *ptr( new StreamingData() );
        auto &port_list( kernel->input );

        for( auto &p : port_list )
        {
            FIFO *fifo( get_FIFO( p.second ) );
            const auto size( fifo->size() );
            if( 0 < size )
            {
                ptr->set( p.first,
                          get_FIFOFunctor( p.second )->peek( fifo ) );
            }
        }
        return( *ptr );
    }


    /**
     * kernel_pack_output_buf - assemble buffer for outputs.
     * @param kernel - raft::Kernel*
     * @return StreamingData.
     */
    static StreamingData &kernel_pack_output_buf( Kernel *kernel )
    {
        auto *ptr( new StreamingData() );
        auto &port_list( kernel->output );

        for( auto &p : port_list )
        {
            FIFO *fifo( get_FIFO( p.second ) );
            const auto cap( fifo->capacity() );
            if( 0 < cap )
            {
                ptr->set( p.first,
                          get_FIFOFunctor( p.second )->allocate( fifo ) );
            }
        }
        return( *ptr );
    }

    /**
     * task_pack_output_buf - assemble buffer for outputs.
     * @param task - raft::Task*
     * @return StreamingData.
     */
    static StreamingData &task_pack_output_buf( Task *task )
    {
        assert( ONE_SHOT == task->type );
        auto &buf( kernel_pack_output_buf( task->kernel ) );
        auto *t( static_cast< OneShotTask* >( task ) );
        t->stream_out = &buf;
        return buf;
    }

    inline void polling_worker_init( PollingWorker *worker )
    {
        auto *kernel( worker->kernel );
        auto nclones( kernel->getCloneFactor() );

        auto *tmeta( new TaskFIFOAllocMeta() );
        worker->alloc_meta = tmeta;

        auto &input_ports( kernel->input );
        tmeta->ports_in.reserve( input_ports.size() );
        for( auto &p : input_ports )
        {
            const auto nfifos( port_fifo[ &p.second ]->size() - 1 );
            const auto fifo_share(
                    nfifos / nclones +
                    ( worker->clone_id < int( nfifos % nclones ) ? 1 : 0 ) );
            tmeta->ports_in.emplace_back( fifo_share );
            tmeta->name2port_in.emplace( p.first, &tmeta->ports_in.back() );
            auto &port( tmeta->ports_in.back() );
            port.idx = 0;
            for( std::size_t i( worker->clone_id ); nfifos > i; i += nclones )
            {
                port.fifos[ port.idx++ ] = port_fifo[ &p.second ]->at( i );
            }
            port.idx = 0;
            port.functor = p.second.runtime_info.fifo_functor;
        }
        // preselect to avoid indexing with string
        if( 1 == tmeta->ports_in.size() )
        {
            tmeta->selected_in = &tmeta->ports_in[ 0 ];
        }

        auto &output_ports( kernel->output );
        tmeta->ports_out.reserve( output_ports.size() );
        for( auto &p : output_ports )
        {
            const auto nfifos( port_fifo[ &p.second ]->size() - 1 );
            const auto fifo_share(
                    nfifos / nclones +
                    ( worker->clone_id < int( nfifos % nclones ) ? 1 : 0 ) );
            tmeta->ports_out.emplace_back( fifo_share );
            tmeta->name2port_out.emplace( p.first, &tmeta->ports_out.back() );
            auto &port( tmeta->ports_out.back() );
            port.idx = 0;
            for( std::size_t i( worker->clone_id ); nfifos > i; i += nclones )
            {
                port.fifos[ port.idx++ ] = port_fifo[ &p.second ]->at( i );
            }
            port.idx = 0;
            port.functor = p.second.runtime_info.fifo_functor;
        }
        // preselect to avoid indexing with string
        if( 1 == tmeta->ports_out.size() )
        {
            tmeta->selected_out = &tmeta->ports_out[ 0 ];
        }
    }

    inline void oneshot_init( OneShotTask *oneshot, bool alloc_input )
    {
        oneshot->stream_in = nullptr;
#if USE_UT
        auto *stream_out_ptr_tmp(
                tcache_alloc( &__perthread_streaming_data_pt ) );
        oneshot->stream_out = new ( stream_out_ptr_tmp ) StreamingData(
                oneshot, StreamingData::OUT_1PIECE );
        //FIXME: here OUT_1PIECE assumes only one output port
        if( alloc_input )
        {
            auto *stream_in_ptr_tmp(
                    tcache_alloc( &__perthread_streaming_data_pt ) );
            oneshot->stream_in = new ( stream_in_ptr_tmp ) StreamingData(
                    oneshot, StreamingData::IN_1PIECE );
        }
#else
        oneshot->stream_out = new StreamingData( oneshot,
                                                 StreamingData::OUT_1PIECE );
        //FIXME: here OUT_1PIECE assumes only one output port
        if( alloc_input )
        {
            oneshot->stream_in = new StreamingData( oneshot,
                                                    StreamingData::IN_1PIECE );
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
        }
    }

    inline void polling_worker_register_consumer( PollingWorker *worker )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( worker->alloc_meta ) );

        auto &input_ports( tmeta->ports_in );
        for( auto &port : input_ports )
        {
            auto *fifos( port.fifos );
            auto nfifos( port.nfifos );
            for( int i = 0; nfifos > i; ++i )
            {
                fifo_tmeta[ fifos[ i ] ] = worker->sched_meta;
            }
        }
    }

    /**
     * kernel_commit - commit the data involved by the kernel compute().
     * @param kernel - raft::Kernel*
     * @return StreamingData.
     */
    static void kernel_commit( Kernel *kernel )
    {
        auto &output_list( kernel->output );

        for( auto &p : output_list )
        {
            get_FIFOFunctor( p.second )->send( get_FIFO( p.second ) );
        }

        auto &input_list( kernel->input );

        for( auto &p : input_list )
        {
            get_FIFOFunctor( p.second )->recycle( get_FIFO( p.second ) );
        }
    }

    /**
     * task_commit - commit the data involved by the kernel compute().
     * @param task - raft::Task*
     * @param buf - raft::StreamingData*
     * @return StreamingData.
     */
    static void task_commit( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            return;
        }
        if( ONE_SHOT == task->type )
        {
            oneshot_commit( static_cast< OneShotTask* >( task ) );
        }
    }

    /**
     * polling_worker_invalidate_outputs - invalidates all the output fifos
     * of a polling worker.
     * @param task - raft::PollingWorker*
     */
    static void polling_worker_invalidate_outputs( PollingWorker *worker )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( worker->alloc_meta ) );
        for( auto &port : tmeta->ports_out )
        {
            for( int i( 0 ); port.nfifos > i; ++i )
            {
                port.fifos[ i ]->invalidate();
                // wake up workers waiting on termination
                if( nullptr != port.tasks[ i ] )
                {
                    port.tasks[ i ]->wakeup();
                }
            }
        }
    }

    /**
     * polling_worker_has_input_ports - if the polling worker has no valid
     * input ports then it returns false.
     * @params   worker - raft::PollingWorker*
     * @return  bool   - false if no valid input ports avail
     */
    static bool polling_worker_has_input_ports( PollingWorker *worker )
    {
        auto *tmeta( static_cast< TaskFIFOAllocMeta* >( worker->alloc_meta ) );
        if( 0 == tmeta->ports_in.size() )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }

        for( auto &port : tmeta->ports_in )
        {
            for( int i( 0 ); port.nfifos > i; ++i )
            {
                if( ! port.fifos[ i ]->is_invalid() )
                {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_map< const PortInfo*, std::vector< FIFO* >* > port_fifo;

    std::unordered_map< FIFO*, TaskSchedMeta* > fifo_tmeta;

#if UT_FOUND
    struct slab streaming_data_slab;
    struct tcache *streaming_data_tcache;
#endif

    volatile bool exited = false;
    volatile bool ready = false;

}; /** end AllocateFIFO decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
