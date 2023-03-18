/**
 * partition_basic.hpp - the most simple of partitioning algorithms.
 * invoked in two cases, if there are fewer or equal to kernels
 * than cores...or if we simply have no other partitioning scheme
 * (library) available.
 *
 * @author: Jonathan Beard
 * @version: Fri Mar 20 08:53:12 2015
 *
 * Copyright 2015 Jonathan Beard
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
#ifndef RAFT_PARTITION_PARTITION_BASIC_HPP
#define RAFT_PARTITION_PARTITION_BASIC_HPP  1
#include <cstdint>
#include <cstddef>
#include <thread>

#include "raftinc/dag.hpp"
#include "raftinc/partition/partition.hpp"

namespace raft
{

class PartitionBasic : public Partition
{
public:
    PartitionBasic( int ngroups = 0 ) : Partition(), num_groups( ngroups ) {}
    virtual ~PartitionBasic() = default;

    virtual DAG & partition( DAG &dag )
    {
        if( 0 == num_groups )
        {
            num_groups = detect_num_groups();
        }
        /* assuming all kernels are chained in a pipeline,
         * get nearby stages into a group. */
        int kernels_per_group =
            ( dag.getKernels().size() + num_groups - 1 ) / num_groups;

        auto func = [kernels_per_group]( Kernel* k, void *data )
        {
            static int cnt = 0;
            Partition::set_group_for_kernel( *k, 1 + cnt / kernels_per_group );
            cnt++;
        };

        GraphTools::BFS( dag.getSourceKernels(), func, nullptr );

        return dag;
    }

private:
    int num_groups;
}; /** end PartitionBasic decl **/

} /** end namespace raft **/

#endif /* END RAFT_PARTITION_PARTITION_BASIC_HPP */
