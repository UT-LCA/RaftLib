/**
 * schedule_basic.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Mon Feb 27 17:24:00 2023
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
#ifndef RAFT_SCHEDULE_SCHEDULE_BASIC_HPP
#define RAFT_SCHEDULE_SCHEDULE_BASIC_HPP  1
#include "signalhandler.hpp"
#include "rafttypes.hpp"
#include "defs.hpp"
#include "kernel.hpp"
#include "kernelkeeper.tcc"
#include "sysschedutil.hpp"
#include "dag.hpp"
#include "task.hpp"
#include "allocate/allocate.hpp"
#include "pollingworker.hpp"
#include <affinity>

namespace raft {

struct StdThreadSchedMeta : public TaskSchedMeta
{
    StdThreadSchedMeta( Task *the_task ) :
        TaskSchedMeta( the_task ), th( [ & ](){
                (this)->task->sched_meta = this;
                (this)->task->exe(); } )
    {
    }

    std::thread th;
    /* map every task to a kthread */
};


class ScheduleBasic : public Schedule
{
public:
    ScheduleBasic( DAG &dag, Allocate *the_alloc ) :
        Schedule(), kernels( dag.getKernels() ),
        source_kernels( dag.getSourceKernels() ),
        sink_kernels( dag.getSinkKernels() ),
        alloc( the_alloc )
    {
        handlers.addHandler( signal::quit, quitHandler );
    }
    virtual ~ScheduleBasic() = default;

    /**
     * schedule - called to start execution of all
     * kernels.  Implementation specific so it
     * is purely virtual.
     */
    virtual void schedule()
    {
        while( ! alloc->isReady() )
        {
            raft::yield();
        }

        auto &container( kernels.acquire() );
        for( auto * const k : container )
        {
            start_polling_worker( k );
        }
        kernels.release();

        bool keep_going( true );
        while( keep_going )
        {
            while( ! tasks_mutex.try_lock() )
            {
                raft::yield();
            }
            //exit, we have a lock
            keep_going = false;
            TaskSchedMeta* tparent( tasks );
            //loop over each thread and check if done
            while( nullptr != tparent->next )
            {
                auto *tmeta( reinterpret_cast< StdThreadSchedMeta* >(
                            tparent->next ) );
                if( tmeta->finished )
                {
                    tmeta->th.join();
                    tparent->next = tmeta->next;
                    delete tmeta;
                }
                else /* a task ! finished */
                {
                    tparent = tmeta;
                    keep_going = true;
                }
            }
            //if we're here we have a lock and need to unlock
            tasks_mutex.unlock();
            /**
             * NOTE: added to keep from having to unlock these so frequently
             * might need to make the interval adjustable dep. on app
             */
            std::chrono::milliseconds dura( 3 );
            std::this_thread::sleep_for( dura );
        }
        return;
    }


    virtual bool shouldExit( Task* task )
    {
        if( kernel_has_no_input_ports( task->kernel ) &&
            ! kernel_has_input_data( task->kernel ) )
        {
            return true;
        }
        return task->sched_meta->finished;
    }


    virtual bool readyRun( Task* task )
    {
        return ( task->kernel->pop( task, true ) &&
                 task->kernel->allocate( task, true ) );
    }


    virtual void precompute( Task* task )
    {
        //std::cout << task->id << std::endl;
    }


    virtual void postcompute( Task* task, const kstatus::value_t sig_status )
    {
        Singleton::allocate()->commit( task );
        if( kstatus::stop == sig_status )
        {
            // indicate a source task should exit
            task->sched_meta->finished = true;
        }
    }

    virtual void reschedule( Task* task )
    {
        raft::yield();
    }

    virtual void prepare( Task* task )
    {
        while( ! Singleton::allocate()->isReady() )
        {
            raft::yield();
        }
        // this scheduler assume 1 pollingworker per kernel
        // so we just take the per-port FIFO as the task FIFO
        copy_port_fifos( task );
    }

    virtual void postexit( Task* task )
    {
        invalidate_output_ports( task->kernel );
        task->sched_meta->finished = true;
    }

protected:

    virtual void start_polling_worker( Kernel * const kernel )
    {
        /**
         * TODO: lets add the affinity dynamically here
         */
        //auto * const th_info( new task_data_t( kernel ) );
        //th_info->data.loc = kernel->getCoreAssignment();

        /**
         * thread function takes a reference back to the scheduler
         * accessible done boolean flag, essentially when the
         * kernel is done, it can be rescheduled...and this
         * handles that.
         */
        PollingWorker *task = new PollingWorker();
        task->kernel = kernel;
        task->id = kernel->getId();
        task->type = POLLING_WORKER;

        while( ! tasks_mutex.try_lock() )
        {
            raft::yield();
        }
        task_id = ( task_id <= task->id ) ? ( task->id + 1 ) : task_id;
        auto *tmeta( new StdThreadSchedMeta( task ) );
        /* insert into tasks linked list */
        tmeta->next = tasks->next;
        tasks->next = tmeta;
        /** we got here, unlock **/
        tasks_mutex.unlock();

        return;
    }

    static inline FIFO* get_FIFO( PortInfo &pi )
    {
        return pi.runtime_info.fifo;
    }

    static inline void copy_port_fifos( Task *task )
    {
        auto *worker( reinterpret_cast< PollingWorker* >( task ) );
        auto *kernel( worker->kernel );
        auto &input_ports( kernel->input );
        for( auto &[ name, info ] : input_ports )
        {
            worker->fifos_in.insert(
                    std::make_pair( name, get_FIFO( info ) ) );
            worker->names_in.push_back( name );
        }
        auto &output_ports( kernel->output );
        for( auto &[ name, info ] : output_ports )
        {
            worker->fifos_out.insert(
                    std::make_pair( name, get_FIFO( info ) ) );
            worker->names_out.push_back( name );
        }
    }


    static void invalidate_output_ports( Kernel *kernel )
    {
        auto &output_ports( kernel->output );
        for( auto &[ name, info ] : output_ports )
        {
            get_FIFO( info )->invalidate();
        }
        return;
    }

    /**
     * kernelHasInputData - check each input port for available
     * data, returns true if any of the input ports has available
     * data.
     * @param kernel - raft::kernel
     * @return bool  - true if input data available.
     */
    static bool kernel_has_input_data( Kernel *kernel )
    {
        auto &port_list( kernel->input );
        if( 0 == port_list.size() )
        {
           /** only output ports, keep calling till exits **/
           return( true );
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
     * kernelHasNoInputPorts - pretty much exactly like the
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
     * checkSystemSignal - check the incomming streams for
     * the param kernel for any system signals, if there
     * is one then consume the signal and perform the
     * appropriate action.
     * @param kernel - raft::kernel::value_t
     * @param data   - void*, use this if any further info
     *  is needed in future implementations of handlers
     * @return  raft::kstatus, proceed unless a stop signal is received
     */
    static kstatus::value_t checkSystemSignal( Kernel * const kernel,
                                               void *data,
                                               SignalHandler &handlers )
    {
        auto &input_ports( kernel->input );
        kstatus::value_t ret_signal( kstatus::proceed );
        for( auto &[ name, info ] : input_ports )
        {
            if( get_FIFO( info )->size() == 0 )
            {
                continue;
            }
            const auto curr_signal( get_FIFO( info )->signal_peek() );
            if( R_UNLIKELY(
                ( curr_signal > 0 &&
                  curr_signal < signal::MAX_SYSTEM_SIGNAL ) ) )
            {
                get_FIFO( info )->signal_pop();
                /**
                 * TODO, right now there is special behavior for term signal
                 * only, what should we do with others?  Need to decide that.
                 */

                if( handlers.callHandler( curr_signal,
                                          *get_FIFO( info ),
                                          kernel,
                                          data ) == kstatus::stop )
                {
                    ret_signal = kstatus::stop;
                }
            }
        }
        return( ret_signal );
    }

    /**
     * quiteHandler - performs the actions needed when
     * a port sends a quite signal (normal termination),
     * this is most likely due to the end of data.
     * @param fifo - FIFO& that sent the signal
     * @param kernel - raft::kernel*
     * @param signal - raft::signal
     * @param data   - void*, vain attempt to future proof
     */
    static kstatus::value_t quitHandler( FIFO &fifo,
                                         Kernel *kernel,
                                         const signal::value_t signal,
                                         void *data )
    {
        /**
         * NOTE: This should be the only action needed
         * currently, however that may change in the future
         * with more features and systems added.
         */
        UNUSED( kernel );
        UNUSED( signal );
        UNUSED( data );

        fifo.invalidate();
        return( kstatus::stop );
    }


    /**
     * signal handlers
     */
    SignalHandler handlers;

    /** kernel set **/
    kernelkeeper kernels;
    kernelkeeper source_kernels;
    kernelkeeper sink_kernels;

    Allocate *alloc;
};

} /** end namespace raft **/
#endif /* END RAFT_SCHEDULE_SCHEDULE_BASIC_HPP */
