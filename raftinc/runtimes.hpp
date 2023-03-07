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

#include "dag.hpp"
#include "runtime.hpp"
#include "partition/partitioners.hpp"
#include "allocate/allocators.hpp"
#include "schedule/schedulers.hpp"

namespace raft
{

class RuntimeFIFO : public RuntimeBase
{
public:

    RuntimeFIFO( DAG &the_dag ) : RuntimeBase( the_dag ) {}

    /**
     * run - function to be extended for the actual execution of the DAG.
     */
    virtual void run()
    {
        PartitionBasic partitioner;
        auto &dag_partitioned( partitioner.partition( dag ) );

        AllocateFIFO allocator( dag_partitioned );
        auto &dag_allocated( allocator.allocate( dag_partitioned ) );

        ScheduleBasic scheduler( dag_allocated, &allocator );
        scheduler.schedule();
    }

};

} /** end namespace raft **/

#endif /* END RAFT_RUNTIMES_HPP */
