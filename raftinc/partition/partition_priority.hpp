/**
 * partition_priority.hpp - parition the DAG based on the priority
 * factors from users' hint.
 *
 * @author: Qinzhe Wu
 * @version: Fri Mar 17 14:49:12 2023
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
#ifndef RAFT_PARTITION_PARTITION_PRIORITY_HPP
#define RAFT_PARTITION_PARTITION_PRIORITY_HPP  1
#include <cstdint>
#include <cstddef>
#include <vector>
#include <algorithm>

#include "raftinc/port_info.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/partition/partition.hpp"

namespace raft
{

class PartitionPriority : public Partition
{
public:
    PartitionPriority( int ngroups = 0 ) :
        Partition(), num_groups( ngroups ) {}
    virtual ~PartitionPriority() = default;

    virtual DAG & partition( DAG &dag )
    {
        if( 0 == num_groups )
        {
            num_groups = detect_num_groups();
        }
        /* assuming at most one connection between two kernels,
         * use union find to keep track of number of groups */
        std::vector< int > parent( dag.getKernels().size() + 1 );
        for( int i( 1 ); parent.size() > (std::size_t)i; ++i )
        {
            parent[ i ] = i;
        }
        auto find_root = [ &parent ]( int i ){
            while( parent[ i ] != i )
            {
                i = parent[ i ];
            }
            return i;
        };

        int ngroups = parent.size() - 1;
        int priority = 1;
        while( num_groups < ngroups )
        {
            // graduately include edges from Priority 1 to Priority N
            const auto &ports( dag.getPortsAtPriority( priority++ ) );
            for( const auto *port : ports )
            {
                int src_id = port->my_kernel->getId();
                int dst_id = port->other_kernel->getId();
                int src_root = find_root( src_id );
                int dst_root = find_root( dst_id );
                if( src_root != dst_root )
                {
                    parent[ std::max( src_root, dst_root ) ] =
                        std::min( src_root, dst_root );
                    if( --ngroups <= num_groups )
                    {
                        // stop once connected groups is lower the given
                        break;
                    }
                }
            }
        }

        std::vector< int > group_id( parent.size() );
        ngroups = 1; // number of groups assigned so far
        for( int i( 1 ); group_id.size() > (std::size_t)i; ++i )
        {
            int root = find_root( i );
            if( i == root )
            {
                group_id[ i ] = ngroups++;
            }
            else
            {
                group_id[ i ] = group_id[ root ];
            }
        }

        auto &kernels( dag.getKernels() );
        for( auto *k : kernels )
        {
            Partition::set_group_for_kernel( *k, group_id[ k->getId() ] );
        }

        return dag;
    }

private:
    int num_groups;
}; /** end PartitionBasic decl **/

} /** end namespace raft **/

#endif /* END RAFT_PARTITION_PARTITION_BASIC_HPP */
