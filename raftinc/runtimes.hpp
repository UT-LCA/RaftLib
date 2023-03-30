/**
 * runtimes.hpp - instantiate different runtimes
 * @author: Qinzhe Wu
 * @version: Wed Mar 01 20:38:00 2023
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
#ifndef RAFT_RUNTIMES_HPP
#define RAFT_RUNTIMES_HPP  1

#if UT_FOUND
#include <ut>
#endif

#include "raftinc/dag.hpp"
#include "raftinc/runtime.hpp"
#include "raftinc/partition/partitioners.hpp"
#include "raftinc/allocate/allocators.hpp"
#include "raftinc/schedule/schedulers.hpp"

namespace raft
{

template< class PARTITIONER, class SCHEDULER >
class RuntimeFIFOTemp : public RuntimeBase
{
public:

    RuntimeFIFOTemp( DAG &the_dag ) : RuntimeBase( the_dag ) {}

    /**
     * run - function to be extended for the actual execution of the DAG.
     */
    virtual void run()
    {
        PARTITIONER partitioner;
        AllocateFIFO allocator;
        SCHEDULER scheduler;

#if USE_UT
        runtime_set_initializers( global_initializer,
                                  perthread_initializer,
                                  late_initializer );
        const auto ret_val( runtime_initialize( NULL ) );
        // with cfg_path set to NULL, libut would getenv("LIBUT_CFG")
        if( 0 != ret_val )
        {
            std::cerr << "failure to initialize libut runtime, existing\n";
            exit( EXIT_FAILURE );
        }
#endif
        auto &dag_partitioned( partitioner.partition( dag ) );

        auto &dag_allocated( allocator.allocate( dag_partitioned ) );

        scheduler.schedule( dag_allocated );
    }

protected:

#if UT_FOUND
    static int global_initializer()
    {
        Singleton::allocate()->globalInitialize();
        Singleton::schedule()->globalInitialize();
        return 0;
    }

    static int perthread_initializer()
    {
        Singleton::allocate()->perthreadInitialize();
        Singleton::schedule()->perthreadInitialize();
        return 0;
    }

    static int late_initializer()
    {
        return 0;
    }
#endif

};

using RuntimeFIFO = RuntimeFIFOTemp< PartitionBasic, ScheduleBasic >;
using RuntimeFIFOOneShot = RuntimeFIFOTemp< PartitionBasic, ScheduleOneShot >;
using RuntimeFIFOCV = RuntimeFIFOTemp< PartitionBasic, ScheduleCV >;
using RuntimeFIFOGroup = RuntimeFIFOTemp< PartitionPriority, ScheduleBasic >;
using RuntimeFIFOGroupCV = RuntimeFIFOTemp< PartitionPriority, ScheduleCV >;

} /** end namespace raft **/

#endif /* END RAFT_RUNTIMES_HPP */
