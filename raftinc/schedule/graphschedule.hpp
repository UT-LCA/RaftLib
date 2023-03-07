/**
 * graphschedule.hpp - scheduler similar to poolschedule but
 * considers the producer-consumer relationship in the graph to
 * guide the task scheduling more efficiently.
 *
 * @author: Qinzhe Wu
 * @version: Sat Feb 18 12:54:00 2023
 *
 * Copyright 2023 Qinzhe Wu
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
#ifndef RAFTGRAPHSCHEDULE_HPP
#define RAFTGRAPHSCHEDULE_HPP  1
#include <vector>
#include <set>
#include <thread>
#include <mutex>
#include <cstdint>
#if UT_FOUND
#include <ut>
#endif
#if QTHREAD_FOUND
#include <qthread/qthread.hpp>
#else
/** dummy **/
using aligned_t = std::uint64_t;
#endif
#include "schedule.hpp"
#include "internaldefs.hpp"
#include "graphtools.hpp"

namespace raft
{
    class kernel;
}


class graph_schedule : public Schedule
{
    static void main_wrapper( void* arg )
    {
        graph_schedule *this_ptr = (graph_schedule*) arg;
        this_ptr->main_handler();
    }

    void main_handler() {
        auto &container( this->kernel_set.acquire() );
#if USE_UT
        waitgroup_init(&wg);
        waitgroup_add(&wg, dst_kernels.size());
#endif
        for( auto * const k : container )
        {
            (this)->handleSchedule( k );
        }
        /**
         * NOTE: can't quite get the sync object behavior to work
         * quite well enough for this application. Should theoretically
         * work according to the documentation here:
         * http://www.cs.sandia.gov/qthreads/man/qthread_spawn.html
         * however it seems that the wait segfaults. Adding on the
         * TODO list I'll implement a better mwait monitor vs. spinning
         * which is relatively bad.
         */
        this->kernel_set.release();
#if USE_UT
        waitgroup_wait(&wg);
#else
        do
        {
            std::chrono::milliseconds dura( 3 );
            std::this_thread::sleep_for( dura );
            bool all_finished = true;
            this->tail_mutex.lock();
            for( auto * const td : this->tail )
            {
                if( ! td->finished )
                {
                    all_finished = false;
                    break;
                }
            }
            this->tail_mutex.unlock();
            if( all_finished ) {
                break;
            }
        } while( true );
#endif // USE_UT
    }
public:
    /**
     * graph_schedule - constructor, takes a map object.
     * It sets up the condition variables for each kernel,
     * as well as the producer-consumer information.
     * @param   map - raft::map&
     */
    graph_schedule( MapBase &map ) : Schedule( map )
    {
#if USE_QTHREAD
        const auto ret_val( qthread_initialize() );
        if( ret_val != 0 )
        {
            std::cerr << "failure to initialize qthreads runtime, exiting\n";
            exit( EXIT_FAILURE );
        }
#endif
        thread_data_pool.reserve( kernel_set.size() );
        auto &source_k( source_kernels.acquire() );
        void * const td_pool_ptr(
                reinterpret_cast< void* >( &thread_data_pool ) );
        GraphTools::BFS( source_k,
                []( PortInfo& a, PortInfo& b, void* data )
                {
                    auto * const td_pool_ptr(
                            reinterpret_cast< thread_data_pool_t* >( data ) );
                    if( 0 == td_pool_ptr->count( a.my_kernel ) )
                    {
                        auto *td( new thread_data( a.my_kernel ) );
                        td_pool_ptr->insert(
                                std::make_pair( a.my_kernel, td ) );
                    }
                    if( 0 == td_pool_ptr->count( b.my_kernel ) )
                    {
                        auto *td( new thread_data( b.my_kernel ) );
                        td_pool_ptr->insert(
                                std::make_pair( b.my_kernel, td ) );
                    }
                    td_pool_ptr->at( a.my_kernel )->cons =
                        td_pool_ptr->at( b.my_kernel );
                    td_pool_ptr->at( b.my_kernel )->prod =
                        td_pool_ptr->at( a.my_kernel );
                    std::cout << std::hex << (uint64_t)td_pool_ptr->at( a.my_kernel ) << " -> " <<
                        (uint64_t)td_pool_ptr->at( b.my_kernel ) << std::dec << std::endl;
                },
                td_pool_ptr,
                false
                );
        source_kernels.release();
    }

    /**
     * destructor, deletes threads and cleans up container
     * objects.
     */
    virtual ~graph_schedule()
    {
#if USE_QTHREAD
        /** kill off the qthread structures **/
        qthread_finalize();
#endif
    }

    /**
     * start - call to start executing map, at this point
     * the mapper sould have checked the topology and
     * everything should be set up for running.
     */
    virtual void start()
    {
#ifdef USE_UT
        const auto ret_val( runtime_initialize( NULL ) );
        // with cfg_path set to NULL, libut would getenv("LIBUT_CFG")
        if ( ret_val != 0 )
        {
            std::cerr << "failure to initialize libut runtime, exiting\n";
            exit( EXIT_FAILURE );
        }
        runtime_start( main_wrapper, this );
#elif defined(USE_QTHREAD)
        main_handler();
#endif
        return;
    }

protected:
    /**
     * modified version of what is in the simple_schedule
     * since we don't really need some of the info. this
     * is passed to each kernel within the task_run func
     */
    struct ALIGN( 64 ) thread_data
    {
#pragma pack( push, 1 )
        thread_data( raft::kernel * const k ) : k( k ){}

        inline void setCore( const core_id_t core ){ loc = core; };
        /** this is deleted elsewhere, do not delete here, bad things happen **/
        raft::kernel *k = nullptr;
#if USE_UT
        waitgroup_t *wg = nullptr;
        rt::Mutex mu;
        rt::CondVar cv;
#else
        bool finished = false;
#endif
        core_id_t loc = -1;
        thread_data *prod = nullptr;
        thread_data *cons = nullptr;
#pragma pack( pop )
    };

    using thread_data_pool_t =
        std::unordered_map< raft::kernel*, thread_data* >;

    /** BEGIN FUNCTIONS **/
    /**
     * handleSchedule - handle actions needed to schedule the
     * kernel. This is mean to make maintaining the code a bit
     * easier.
     * @param    kernel - kernel to schedule
     */
    virtual void handleSchedule( raft::kernel * const kernel )
    {
        auto *td( thread_data_pool[kernel] );
        if( ! kernel->output.hasPorts() ) /** has no outputs **/
        {
#if USE_UT
            td->wg = &wg;
#else
            std::lock_guard< std::mutex > tail_lock( tail_mutex );
            /** destination kernel **/
            tail.emplace_back( td );
#endif
        }
#if USE_QTHREAD
        qthread_spawn( graph_schedule::task_run,
                       (void*) td,
                       0,
                       0,
                       0,
                       nullptr,
                       NO_SHEPHERD,
                       0 );
#elif USE_UT
        thread_t *th_ptr = rt::Spawn(
                [td](){ graph_schedule::task_run(td); } );
#endif
        /** done **/
        return;
    }

    /**
     * task_run - pass this to the user space threading library to run task.
     */
    static aligned_t task_run( void *data )
    {
       assert( data != nullptr );
       auto * const thread_d( reinterpret_cast< thread_data* >( data ) );
       ptr_map_t in;
       ptr_set_t out;
       ptr_set_t peekset;

       Schedule::setPtrSets( thread_d->k,
                             &in,
                             &out,
                             &peekset );
#if 0 //figure out pinning later
       if( thread_d->loc != -1 )
       {
          /** call does nothing if not available **/
          raft::affinity::set( thread_d->loc );
       }
       else
       {
#ifdef USE_PARTITION
           assert( false );
#endif
       }
#endif
       volatile bool done( false );
       std::uint8_t run_count( 0 );
#ifdef BENCHMARK
       const auto init_id( raft::kernel::initialized_count() );
       while( raft::kernel::initialized_count( 0 ) !=
              raft::kernel::kernel_count( 0 ) )
       {
           raft::yield();
       }
#endif
       while( ! done )
       {
           Schedule::kernelRun( thread_d->k, done );
           //FIXME: add back in SystemClock user space timer
           //set up one cache line per thread
           if( run_count++ == 20 || done )
           {
               run_count = 0;
               //takes care of peekset clearing too
               Schedule::fifo_gc( &in, &out, &peekset );
#if USE_UT
               //source kernel only yield
               if( nullptr == thread_d->prod || nullptr == thread_d->cons )
               {
                   raft::yield();
               }
               else if ( thread_d->mu.TryLock() )
               {
                   thread_d->cv.Wait( &thread_d->mu );
                   thread_d->mu.Unlock();
               }
               //TODO: find a way to check the FIFO and weak only the needed
               if( nullptr != thread_d->cons && thread_d->cons->mu.TryLock() )
               {
                   thread_d->cons->cv.Signal();
                   thread_d->cons->mu.Unlock();
               }
               if( nullptr != thread_d->prod && thread_d->prod->mu.TryLock() )
               {
                   thread_d->prod->cv.Signal();
                   thread_d->prod->mu.Unlock();
               }
#else
               raft::yield();
#endif
           }
       }
#if USE_UT
       if( thread_d->wg )
       {
           // only tail kernel has wg pointer assigned
           waitgroup_done( thread_d->wg );
       }
       // before termination, make sure the consumer runs
       if( nullptr != thread_d->cons )
       {
           while( !thread_d->cons->mu.TryLock() )
           {
               raft::yield();
           }
           std::cout << std::hex << (uint64_t)thread_d << " wakes up " << (uint64_t)thread_d->cons << std::dec << " before exiting\n";
           thread_d->cons->cv.Signal();
           thread_d->cons->mu.Unlock();
       }
#else
       thread_d->finished = true;
#endif
       return( 1 );
    }

    thread_data_pool_t thread_data_pool;
#if USE_UT
    waitgroup_t wg;
#else
    std::mutex tail_mutex;
    std::vector< thread_data* > tail;
#endif
};
#endif /* END RAFTGRAPHSCHEDULE_HPP */
