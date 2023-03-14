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

#include <unordered_set>

#include "defs.hpp"
#include "dag.hpp"
#include "kernel.hpp"
#include "port_info.hpp"
#include "exceptions.hpp"
#include "pollingworker.hpp"
#include "oneshottask.hpp"
#include "allocate/allocate.hpp"
#include "allocate/fifo.hpp"
#include "allocate/ringbuffer.tcc"
#include "allocate/buffer/buffertypes.hpp"

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
     * AllocateFIFO - base constructor, really doesn't do too much
     * save for setting the global variables all_kernels and
     * source_kernels from the DAG object.
     * @param dag - raft::DAG&
     */
    AllocateFIFO( DAG &dag ) :
        Allocate(), kernels( dag.getKernels() ),
        source_kernels( dag.getSourceKernels() )
    {
    }

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
     * runThread - will run as a thread.
     */
    virtual void runThread()
    {
        /** launch allocator in a thread **/
        std::thread alloc_thread( [&](){
            (this)->allocate();
            /* thread exit after allocating initial FIFOs */
            (this)->exited = true;
        } );
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

    virtual void allocate()
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
                }
            }
            /* allocate one more fifo as the mutex-protected fifo */
            fifos->back() =
                get_FIFOFunctor(
                        a )->make_new_fifo_mutexed( INITIAL_ALLOC_SIZE,
                                                    ALLOC_ALIGN_WIDTH,
                                                    nullptr );
            a.runtime_info.fifo = b.runtime_info.fifo = fifos->at( 0 );
        };

        GraphTools::BFS( (this)->source_kernels, func );

        (this)->ready = true;
    }

    virtual DAG &allocate( DAG &dag )
    {
        allocate();
        return dag;
    }

    virtual bool dataInReady( Task *task, const port_name_t &name )
    {
        return task_has_input_data( task );
        //return kernel_has_input_data( task->kernel );
    }

    virtual bool bufOutReady( Task *task, const port_name_t &name )
    {
        return kernel_has_output_buf( task->kernel );
    }

    virtual bool getDataIn( Task *task, const port_name_t &name )
    {
        return task_has_input_data( task, name );
    }

    virtual bool getBufOut( Task *task, const port_name_t &name )
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

    virtual void taskInit( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( reinterpret_cast< PollingWorker* >( task ) );
            polling_worker_init( t );
        }
        else if( ONE_SHOT == task->type )
        {
            auto *t( reinterpret_cast< OneShotTask* >( task ) );
            oneshot_init( t );
        }
    }

    virtual void commit( Task *task )
    {
        task_commit( task );
    }

    virtual void invalidateOutputs( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( reinterpret_cast< PollingWorker* >( task ) );
            polling_worker_invalidate_outputs( t );
        }
        //TODO: design for oneshot task
    }

    virtual bool taskHasInputPorts( Task *task )
    {
        if( POLLING_WORKER == task->type )
        {
            auto *t( reinterpret_cast< PollingWorker* >( task ) );
            polling_worker_has_input_ports( t );
        }
        //TODO: design for oneshot task
    }

    virtual void taskPush( Task *task, const port_name_t &name, DataRef &item )
    {
        auto *functor(
                task->kernel->getOutput( name ).runtime_info.fifo_functor );
        FIFO *fifo;
        if( POLLING_WORKER == task->type )
        {
            auto *worker( reinterpret_cast< PollingWorker* >( task ) );
            auto &fifos( worker->fifos_out[ name ] );
            fifo = fifos[ worker->fifos_out_idx[ name ] ];
            worker->fifos_out_idx[ name ] =
                ( worker->fifos_out_idx[ name ] + 1 ) % fifos.size();
        }
        else /* if( ONE_SHOT == task->type ) */
        {
            auto &fifos( (this)->port_fifo[ &( task->kernel->getOutput( name ) ) ] );
            fifo = fifos->back();
        }
        functor->push( fifo, item );
    }

    virtual DataRef taskAllocate( Task *task, const port_name_t &name )
    {
        auto *functor(
                task->kernel->getOutput( name ).runtime_info.fifo_functor );
        FIFO *fifo;
        if( POLLING_WORKER == task->type )
        {
            auto *worker( reinterpret_cast< PollingWorker* >( task ) );
            auto &fifos( worker->fifos_out[ name ] );
            fifo = fifos[ worker->fifos_out_idx[ name ] ];
        }
        else /* if( ONE_SHOT == task->type ) */
        {
            auto &fifos( (this)->port_fifo[ &( task->kernel->getOutput( name ) ) ] );
            fifo = fifos->back();
        }
        return functor->allocate( fifo );
    }

    virtual void taskSend( Task *task, const port_name_t &name )
    {
        auto *functor(
                task->kernel->getOutput( name ).runtime_info.fifo_functor );
        FIFO *fifo;
        if( POLLING_WORKER == task->type )
        {
            auto *worker( reinterpret_cast< PollingWorker* >( task ) );
            auto &fifos( worker->fifos_out[ name ] );
            fifo = fifos[ worker->fifos_out_idx[ name ] ];
            worker->fifos_out_idx[ name ] =
                ( worker->fifos_out_idx[ name ] + 1 ) % fifos.size();
        }
        else /* if( ONE_SHOT == task->type ) */
        {
            auto &fifos( (this)->port_fifo[ &( task->kernel->getOutput( name ) ) ] );
            fifo = fifos->back();
        }
        functor->send( fifo );
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
                                       const port_name_t &name =
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
                for( auto &[ name, info ] : port_list )
                {
                   const auto size( get_FIFO( info )->size() );
                   if( size > 0 )
                   {
                      return( true );
                   }
                }
            }
            break;
            case( trigger::all_port ):
            {
                for( auto &[ name, info ] : port_list )
                {
                   const auto size( get_FIFO( info )->size() );
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
     * @param name - raft::port_name_t &
     * @return bool  - true if input data available.
     */
    bool task_has_input_data( Task *task,
                              const port_name_t &name = null_port_value )
    {
        auto &port_list( task->kernel->input );
        if( 0 == port_list.size() )
        {
           /** only output ports, keep calling till exits **/
           return( true );
        }

        assert( POLLING_WORKER == task->type );
        auto *worker( reinterpret_cast< PollingWorker* >( task ) );
        if( null_port_value != name )
        {
            auto &fifos( worker->fifos_in[ name ] );
            const int nfifos( fifos.size() );
            const int idx( worker->fifos_in_idx[ name ] );
            for( int i( 0 ); nfifos > i; ++i )
            {
                const auto size( fifos[ ( idx + i ) % nfifos ]->size() );
                if( size > 0 )
                {
                    worker->fifos_in_idx[ name ] = ( idx + i ) % nfifos;
                    return true;
                }
            }
            return false;
        }

        for( auto &name : worker->names_in )
        {
            auto &fifos( worker->fifos_in[ name ] );
            const int nfifos( fifos.size() );
            const int idx( worker->fifos_in_idx[ name ] );
            for( int i( 0 ); nfifos > i; ++i )
            {
                const auto size( fifos[ ( idx + i ) % nfifos ]->size() );
                if( size > 0 )
                {
                    worker->fifos_in_idx[ name ] = ( idx + i ) % nfifos;
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
                                       const port_name_t &name =
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

        for( auto &[ name, info ] : port_list )
        {
           const auto cap( get_FIFO( info )->capacity() );
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
        for( auto &[ name, info ] : port_list )
        {
            if( ! get_FIFO( info )->is_invalid() )
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

        for( auto &[ name, info ] : port_list )
        {
            FIFO *fifo( get_FIFO( info ) );
            const auto size( fifo->size() );
            if( 0 < size )
            {
                ptr->set( name, get_FIFOFunctor( info )->peek( fifo ) );
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

        for( auto &[ name, info ] : port_list )
        {
            FIFO *fifo( get_FIFO( info ) );
            const auto cap( fifo->capacity() );
            if( 0 < cap )
            {
                ptr->set( name, get_FIFOFunctor( info )->allocate( fifo ) );
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
        auto *t( reinterpret_cast< OneShotTask* >( task ) );
        t->stream_out = &buf;
        return buf;
    }

    /**
     * copy_port_fifos - copy the fifo allocated for the ports as the
     * fifo for the task.
     * @param task - raft::Task*
     * @return StreamingData.
     */
    static inline void copy_port_fifos( PollingWorker *worker )
    {
        //auto *kernel( worker->kernel );
        //auto &input_ports( kernel->input );
        //for( auto &[ name, info ] : input_ports )
        //{
        //    worker->fifos_in.insert(
        //            std::make_pair( name, get_FIFO( info ) ) );
        //    worker->names_in.push_back( name );
        //}
        //auto &output_ports( kernel->output );
        //for( auto &[ name, info ] : output_ports )
        //{
        //    worker->fifos_out.insert(
        //            std::make_pair( name, get_FIFO( info ) ) );
        //    worker->names_out.push_back( name );
        //}
    }

    inline void polling_worker_init( PollingWorker *worker )
    {
        auto *kernel( worker->kernel );
        auto nclones( kernel->getCloneFactor() );

        auto &input_ports( kernel->input );
        for( auto &[ name, info ] : input_ports )
        {
            const auto nfifos( port_fifo[ &info ]->size() - 1 );
            for( int i( worker->clone_id ); nfifos > i; i += nclones )
            {
                worker->fifos_in[ name ].push_back(
                        port_fifo[ &info ]->at( i ) );
            }
            worker->fifos_in_idx[ name ] = 0;
            worker->names_in.push_back( name );
        }
        auto &output_ports( kernel->output );
        for( auto &[ name, info ] : output_ports )
        {
            const auto nfifos( port_fifo[ &info ]->size() - 1 );
            for( int i( worker->clone_id ); nfifos > i; i += nclones )
            {
                worker->fifos_out[ name ].push_back(
                        port_fifo[ &info ]->at( i ) );
            }
            worker->fifos_out_idx[ name ] = 0;
            worker->names_out.push_back( name );
        }
    }

    static inline void oneshot_init( OneShotTask *oneshot )
    {
        oneshot->stream_out = new StreamingData();
        auto &output_ports( oneshot->kernel->output );

        for( auto &[ name, info ] : output_ports )
        {
            oneshot->stream_out->set(
                    name, get_FIFOFunctor( info )->oneshot_allocate() );
            /* TODO: needs to find a place to release the malloced data */
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

        for( auto &[ name, info ] : output_list )
        {
            get_FIFOFunctor( info )->send( get_FIFO( info ) );
        }

        auto &input_list( kernel->input );

        for( auto &[ name, info ] : input_list )
        {
            get_FIFOFunctor( info )->recycle( get_FIFO( info ) );
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
    }

    /**
     * polling_worker_invalidate_outputs - invalidates all the output fifos
     * of a polling worker.
     * @param task - raft::PollingWorker*
     */
    static void polling_worker_invalidate_outputs( PollingWorker *worker )
    {
        for( auto &name : worker->names_out )
        {
            for( FIFO *fifo : worker->fifos_out[ name ] )
            {
                fifo->invalidate();
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
        if( 0 == worker->kernel->input.size() )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }

        for( auto &name : worker->names_in )
        {
            for( FIFO *fifo : worker->fifos_in[ name ] )
            {
                if( ! fifo->is_invalid() )
                {
                    return true;
                }
            }
        }
        return false;
    }

    /** both convenience structs, hold exactly what the names say **/
    kernelset_t &kernels;
    kernelset_t &source_kernels;

    /**
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_map< const PortInfo*, std::vector< FIFO* >* > port_fifo;

    volatile bool exited = false;
    volatile bool ready = false;

}; /** end AllocateFIFO decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
