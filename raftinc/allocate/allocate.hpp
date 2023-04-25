/**
 * allocate.hpp - allocate base class, extend me to build new
 * allocate classes.
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:50:21 2023
 * 
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2014 Jonathan Beard
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
#ifndef RAFT_ALLOCATE_ALLOCATE_HPP
#define RAFT_ALLOCATE_ALLOCATE_HPP  1

#include "raftinc/defs.hpp"
#include "raftinc/exceptions.hpp"
#include "raftinc/singleton.hpp"

#if UT_FOUND
#include <ut>
#endif

namespace raft
{

#if UT_FOUND
inline __thread tcache_perthread __perthread_streaming_data_pt;
#endif

class Task;
class DataRef;
class StreamingData;
class PortInfo;

struct TaskAllocMeta
{
    TaskAllocMeta() = default;
    virtual ~TaskAllocMeta() = default;
};

class Allocate
{
public:
    Allocate()
    {
        /* set myself as the singleton allocator */
        Singleton::allocate( this );
    }

    virtual ~Allocate() = default;

    virtual void allocate()
    {
    }

#if UT_FOUND
    virtual void globalInitialize()
    {
    }

    virtual void perthreadInitialize()
    {
    }
#endif

    /**
     * isReady - call after initializing the allocate thread, check if the
     * initial allocation is complete.
     */
    virtual bool isReady() const
    {
        return ready;
    }

    /**
     * isExited - call after initializing the allocate thread, check if the
     * allocate thread has exited.
     */
    virtual bool isExited() const
    {
        return exited;
    }

    virtual bool dataInReady( Task *task, const port_key_t &name ) = 0;
    virtual bool bufOutReady( Task *task, const port_key_t &name ) = 0;

    virtual void taskInit( Task *task, bool alloc_input = false ) = 0;
    virtual void registerWaiter( Task *task ) = 0;
    virtual void taskCommit( Task *task ) = 0;
    virtual void invalidateOutputs( Task *task ) = 0;
    virtual bool taskHasInputPorts( Task *task ) = 0;

    virtual int select( Task *task, const port_key_t &name, bool is_in ) = 0;
    virtual void taskPop( Task *task, int selected, DataRef &item ) = 0;
    virtual DataRef taskPeek( Task *task, int selected ) = 0;
    virtual void taskRecycle( Task *task, int selected ) = 0;
    virtual void taskPush( Task *task, int selected, DataRef &item ) = 0;
    virtual DataRef taskAllocate( Task *task, int selected ) = 0;
    virtual void taskSend( Task *task, int selected ) = 0;

    /* schedPop - used by ScheduleOneShot to drain the output generated by
     * a task in order to feed consumer tasks
     * @param task - Task *
     * @param pi_ptr - PortInfo *&
     * @param ref - DataRef &
     * @param selected - int *
     * @param is_last - bool *
     * @return bool - true when valid output found and set
     */
    virtual bool schedPop( Task *task, PortInfo *&pi_ptr, DataRef &ref,
                           int *selected, bool *is_last = nullptr ) = 0;

protected:

#if UT_FOUND
    struct slab streaming_data_slab;
    struct tcache *streaming_data_tcache;
#endif

    volatile bool ready = false;
    volatile bool exited = false;

}; /** end Allocate decl **/

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_ALLOCATE_HPP */
