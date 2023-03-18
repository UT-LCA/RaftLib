/**
 * partition.hpp -
 *
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 13:14:05 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2016 Jonathan Beard
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
#ifndef RAFT_PARTITION_PARTITION_HPP
#define RAFT_PARTITION_PARTITION_HPP  1

#include "raftinc/defs.hpp"

namespace raft
{

class Partition
{
public:
    Partition() = default;
    virtual ~Partition() = default;

protected:

    template< class K >
    static inline void set_group_for_kernel( K &kernel, int group_id )
    {
        kernel.setGroup( group_id );
    }

    static int detect_num_groups()
    {
        auto *ngroups_env = std::getenv( "RAFT_NUM_GROUPS" );
        if( ngroups_env != nullptr )
        {
            return std::stoi( ngroups_env );
        }
        else
        {
            return std::thread::hardware_concurrency();
        }
    }

}; /** end Partition decl **/

} /** end namespace raft **/
#endif /* END RAFT_PARTITION_PARTITION_HPP */
