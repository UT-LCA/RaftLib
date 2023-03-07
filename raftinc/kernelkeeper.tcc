/**
 * kernelkeeper.tcc - contain the locking and unlocking of
 * containers related to raft compute kernels
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 15:22:01 2023
 *
 * Copyright 2023 The Regents of the University of Texas
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
#ifndef RAFT_KERNELKEEPER_TCC
#define RAFT_KERNELKEEPER_TCC  1
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <functional>
#include <cassert>
#include <cstring>
#include <mutex>

#include "sysschedutil.hpp"

namespace raft
{

class Kernel;

template< class CONTAINER, class ELEMENTTYPE >
class Keeper
{

private:
    std::mutex        mutex;
    CONTAINER         container;

public:

    using value_type =
        typename std::remove_reference< decltype( container ) >::type;

    Keeper() = default;

    Keeper( CONTAINER &container )
    {
        auto &the_container( acquire() );
        for( auto &ele : container )
        {
            the_container.emplace( ele );
        }
        release();
    }

    virtual ~Keeper() = default;

    //FIXME: this will only work for sets, add specializations for others
    //based on insert function
    void operator += ( ELEMENTTYPE * const ele )
    {
        auto &the_container( acquire() );
        the_container.emplace( ele );
        release();
        return;
    }

    void unsafeAdd( ELEMENTTYPE * const ele )
    {
        container.emplace( ele );
        return;
    }

    CONTAINER& acquire()
    {
        //spin until we can get a lock
        while( ! mutex.try_lock() )
        {
           //it's polite to yield
           raft::yield();
        }
        return( container );
    }

    void release()
    {
        mutex.unlock();
        return;
    }

    auto size() -> decltype( container.size() )
    {
        const auto size( container.size() );
        return( size );
    }

};

using kernelkeeper = Keeper< kernelset_t, Kernel >;

} /** end namespace raft **/

#endif /* END RAFT_KERNELKEEPER_TCC */
