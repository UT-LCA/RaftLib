/**
 * singleton.hpp - the singleton for all runtime components which
 * would be accessed here there globally, such as the allocator,
 * and the scheduler
 * @author: Qinzhe Wu
 * @version: Thu Mar 09 21:36:00 2023
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
#ifndef RAFT_SINGLETON_HPP
#define RAFT_SINGLETON_HPP  1

namespace raft
{

class Allocate;
class Schedule;

class Singleton
{
public:

    Singleton() = default;

    static Allocate *allocate( Allocate *the_alloc = nullptr )
    {
        static Allocate *alloc = nullptr;
        alloc = ( nullptr == the_alloc ) ? alloc : the_alloc;
        return alloc;
    }

    static Schedule *schedule( Schedule *the_sched = nullptr )
    {
        static Schedule *sched = nullptr;
        sched = ( nullptr == the_sched ) ? sched : the_sched;
        return sched;
    }

}; /** end Singleton decl **/

} /** end namespace raft **/

#endif /* END RAFT_SINGLETON_HPP */
