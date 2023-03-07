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
        for( auto *p : allocated_fifo )
        {
            delete( p );
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
            a.alloc = this;
            b.alloc = this;
            FIFO *fifo =
                get_FIFOFunctor( a )->make_new_fifo( INITIAL_ALLOC_SIZE,
                                                     ALLOC_ALIGN_WIDTH,
                                                     nullptr );
            a.runtime_info.fifo = b.runtime_info.fifo = fifo;
            (this)->allocated_fifo.insert( fifo );
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
        return kernel_has_input_data( task->kernel );
    }

    virtual bool bufOutReady( Task *task, const port_name_t &name )
    {
        return kernel_has_output_buf( task->kernel );
    }

    virtual bool getDataIn( Task *task, const port_name_t &name )
    {
        return kernel_has_input_data( task->kernel, name );
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
        buf_packed[ task ] = &kernel_pack_output_buf( task->kernel );
        return *buf_packed[ task ];
    }

    virtual void commit( Task *task )
    {
        task_commit( task, buf_packed[ task ] );
        buf_packed[ task ] = nullptr;
        //kernel_commit( task->kernel );
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


    static void invalidate_output_ports( Kernel *kernel )
    {
        auto &output_ports( kernel->output );
        for( auto &[ name, info ] : output_ports )
        {
            //get_FIFOFunctor( info )->invalidate( get_FIFO( info ) );
            get_FIFO( info )->invalidate();
        }
        return;
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
        // ptr->clearTouchedMarks();
        return( *ptr );
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
     * @param kernel - raft::Kernel*
     * @return StreamingData.
     */
    static void task_commit( Task *task, StreamingData *buf )
    {
        auto *kernel( task->kernel );
        auto &output_list( kernel->output );

        for( auto &[ name, info ] : output_list )
        {
            if( buf->kernelTouched( name ) )
            {
                get_FIFOFunctor( info )->send( get_FIFO( info ) );
            }
            else
            {
                get_FIFOFunctor( info )->deallocate( get_FIFO( info ) );
            }
        }

        auto &input_list( kernel->input );

        for( auto &[ name, info ] : input_list )
        {
            get_FIFOFunctor( info )->recycle( get_FIFO( info ) );
        }
    }

    /** both convenience structs, hold exactly what the names say **/
    kernelset_t &kernels;
    kernelset_t &source_kernels;

    /**
     * keeps a list of all currently allocated FIFO objects
     */
    std::unordered_set< FIFO* > allocated_fifo;

    std::unordered_map< Task*, StreamingData* > buf_packed;

    volatile bool exited = false;
    volatile bool ready = false;

}; /** end AllocateFIFO decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_FIFO_HPP */
