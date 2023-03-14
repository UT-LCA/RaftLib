/**
 * partitioners.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 14:50:44 2023
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
#ifndef RAFT_PARTITION_PARTITIONERS_HPP
#define RAFT_PARTITION_PARTITIONERS_HPP  1

/**
 * simply a list of the current partitioners...
 */
#include "raftinc/partition/partition.hpp"
#include "raftinc/partition/partition_basic.hpp"
#if USE_PARTITION
#include "raftind/partition/partition_scotch.hpp"
#endif

#endif /* END RAFT_PARTITION_PARTITIONERS_HPP */
