/**
 * runtime.hpp - define the interfaces of runtime
 * @author: Qinzhe Wu
 * @version: Wed Feb 23 15:06:00 2023
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
#ifndef RAFT_RUNTIME_HPP
#define RAFT_RUNTIME_HPP  1

#include "raftinc/dag.hpp"

namespace raft
{

class RuntimeBase
{
public:
    RuntimeBase( DAG &the_dag ) : dag( the_dag ) {}
    virtual ~RuntimeBase() = default;

protected:

    DAG &dag;
};

template< class PARTITIONER, /* analyze DAG, set proporties for other */
          class ALLOCATOR, /* allocate message queue buffers */
          class SCHEDULER /* schedule tasks */ >
class Runtime : public RuntimeBase
{
public:

    Runtime( DAG &the_dag ) : RuntimeBase( the_dag ) {}

    /**
     * run - function to be extended for the actual execution of the DAG.
     */
    virtual void run()
    {
        PARTITIONER partitioner;
        auto &dag_partitioned( partitioner.partition( dag ) );

        ALLOCATOR allocator;
        auto &dag_allocated( allocator.allocate( dag_partitioned ) );

        SCHEDULER scheduler;
        scheduler.schedule( dag_allocated );
    }

}; /** end Runtime decl **/

} /** end namespace raft **/

#endif /* END RAFT_RUNTIME_HPP */
