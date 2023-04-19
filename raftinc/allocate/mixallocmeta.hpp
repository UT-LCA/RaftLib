/**
 * mixallocmeta.hpp - the meta data structures used by AllocateMix
 * @author: Qinzhe Wu
 * @version: Fri Apr 07 09:34:06 2023
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
#ifndef RAFT_ALLOCATE_MIXALLOCMETA_HPP
#define RAFT_ALLOCATE_MIXALLOCMETA_HPP  1

#include <unordered_map>

#if UT_FOUND
#include <ut>
#endif

#include "raftinc/defs.hpp"
#include "raftinc/port_info.hpp"
#include "raftinc/streamingdata.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/ringbuffer.tcc"
#include "raftinc/allocate/allocate_new.hpp"
#include "raftinc/allocate/functors.hpp"

namespace raft
{

struct MixAllocMeta : public TaskAllocMeta
{
    MixAllocMeta() : TaskAllocMeta() {}
    virtual ~MixAllocMeta() = default;

    /* Interfaces from FIFOAllocMeta */
    virtual int selectIn( const port_key_t &name ) = 0;
    virtual int selectOut( const port_key_t &name ) = 0;
    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const = 0;
    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false ) = 0;
    virtual void nextFIFO( int selected ) = 0;
    virtual FIFO *wakeupConsumer( int selected ) const = 0;
    virtual void setConsumer( int selected, CondVarWorker *worker ) = 0;
    virtual void invalidateOutputs() const = 0;
    virtual bool hasValidInput() const = 0;
    virtual bool hasInputData( const port_key_t &name ) = 0;
    virtual bool isStatic() const = 0;

    /* Extended interfaces */

    /* pushOutBuf - append data to outbufs list
     * @param ref - DataRef &
     * @param selected - int
     */
    virtual void pushOutBuf( DataRef &ref,
                             int selected ) = 0;

    /* allocateOutBuf - allocate an output buffer in outbufs list
     * @param ref - DataRef &
     * @param selected - int
     */
    virtual DataRef allocateOutBuf( int selected ) = 0;

    /* popOutBuf - used by schedPop to get the DataRef and PortInfo
     * of the task being drain
     * @param pi_ptr - PortInfo *&
     * @param ref - DataRef &
     * @param is_last - bool *
     * @return bool - true for valid data popped
     */
    virtual bool popOutBuf( PortInfo *& pi_ptr,
                            DataRef &ref,
                            bool *is_last ) = 0;

};

/* KernelMixAllocMeta - Hold all read-only data for a kernel,
 * might be assigned to a task if no multiple fifos on any of the port
 */
struct KernelMixAllocMeta : public MixAllocMeta
{
    KernelMixAllocMeta( Kernel *k ) : kfifometa( k )
    {
    }

    virtual ~KernelMixAllocMeta() = default;

    KernelFIFOAllocMeta kfifometa;

    virtual int selectIn( const port_key_t &name )
    {
        return kfifometa.selectIn( name );
    }

    virtual int selectOut( const port_key_t &name )
    {
        return kfifometa.selectOut( name );
    }

    virtual PortInfo **getPortsInInfo() const
    {
        return kfifometa.getPortsInInfo();
    }

    virtual PortInfo **getPortsOutInfo() const
    {
        return kfifometa.getPortsOutInfo();
    }

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const
    {
        kfifometa.getPairIn( functor, fifo, selected );
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false )
    {
        UNUSED( is_oneshot );
        auto *pi( kfifometa.ports_out_info[ selected ] );
#if IGNORE_HINT_0CLONE
#else
        if( 0 == pi->runtime_info.nfifos )
        {
            return false;
            /* switch to OneShot since kernel has zero polling worker */
        }
#endif
        fifo = pi->runtime_info.fifos[ 0 ];
#if IGNORE_HINT_FULLQ
#else
        if( 0 == fifo->space_avail() )
        {
            return false;
        }
#endif
        functor = pi->runtime_info.fifo_functor;
        return true;
    }

    virtual void nextFIFO( int selected )
    {
        UNUSED( selected );
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
        UNUSED( selected );
        return nullptr;
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
        UNUSED( selected );
        UNUSED( worker );
    }

    virtual void invalidateOutputs() const
    {
        kfifometa.invalidateOutputs();
    }

    virtual bool hasValidInput() const
    {
        return kfifometa.hasValidInput();
    }

    virtual bool hasInputData( const port_key_t &name )
    {
        return kfifometa.hasInputData( name );
    }

    virtual bool isStatic() const
    {
        return true;
    }

    virtual void pushOutBuf( DataRef &ref, int selected )
    {
        UNUSED( ref );
        UNUSED( selected );
    }

    virtual DataRef allocateOutBuf( int selected )
    {
        UNUSED( selected );
        return DataRef();
    }

    virtual bool popOutBuf( PortInfo *&pi_ptr, DataRef &ref, bool *is_last )
    {
        UNUSED( pi_ptr );
        UNUSED( ref );
        UNUSED( is_last );
        return false;
    }
};

struct OneShotMixAllocMeta : public MixAllocMeta
{
    OneShotMixAllocMeta( KernelMixAllocMeta & meta ) :
        MixAllocMeta(), kmeta( meta )
    {
    }

    virtual ~OneShotMixAllocMeta() = default;

    KernelMixAllocMeta &kmeta;

    virtual int selectIn( const port_key_t &name )
    {
        return kmeta.selectIn( name );
    }

    virtual int selectOut( const port_key_t &name )
    {
        return kmeta.selectOut( name );
    }

    virtual PortInfo **getPortsInInfo() const
    {
        return kmeta.getPortsInInfo();
    }

    virtual PortInfo **getPortsOutInfo() const
    {
        return kmeta.getPortsOutInfo();
    }

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const
    {
        kmeta.getPairIn( functor, fifo, selected );
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false )
    {
        UNUSED( functor );
        UNUSED( fifo );
        UNUSED( selected );
        UNUSED( is_oneshot );
        return false; /* OneShotMixAllocMeta used by OneShotTask only */
    }

    virtual void nextFIFO( int selected )
    {
        UNUSED( selected );
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
        UNUSED( selected );
        return nullptr;
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
        UNUSED( selected );
        UNUSED( worker );
    }

    virtual void invalidateOutputs() const
    {
        /* do nothing */
    }

    virtual bool hasValidInput() const
    {
        return true;
    }

    virtual bool hasInputData( const port_key_t &name )
    {
        UNUSED( name );
        return true;
    }

    virtual bool isStatic() const
    {
        return false;
    }

    virtual void pushOutBuf( DataRef &ref, int selected )
    {
        auto *node( new BufListNode() );
        node->pi = kmeta.kfifometa.ports_out_info[ selected ];
        auto *functor( node->pi->runtime_info.bullet_functor );
        node->ref = functor->allocate( ref );
        // insert into the linked list
        node->next = outbufs.next;
        outbufs.next = node;
    }

    virtual DataRef allocateOutBuf( int selected )
    {
        auto *node( new BufListNode() );
        node->pi = kmeta.kfifometa.ports_out_info[ selected ];
        auto *functor( node->pi->runtime_info.bullet_functor );
        node->ref = functor->allocate();
        // insert into the linked list
        node->next = outbufs.next;
        outbufs.next = node;
        return node->ref;
    }

    virtual bool popOutBuf( PortInfo *&pi_ptr, DataRef &ref, bool *is_last )
    {
        if( nullptr == outbufs.next )
        {
            return false;
        }
        auto *node( outbufs.next );
        pi_ptr = node->pi;
        ref = node->ref;
        outbufs.next = node->next;
        delete node;
        *is_last = ( nullptr == outbufs.next );
        return true;
    }

    BufListNode outbufs;
};

struct RRWorkerMixAllocMeta : public OneShotMixAllocMeta
{
    RRWorkerMixAllocMeta( KernelMixAllocMeta &meta, int rr_idx ) :
        OneShotMixAllocMeta( meta ), rrmeta( meta.kfifometa, rr_idx )
    {
    }

    virtual ~RRWorkerMixAllocMeta() = default;

    RRTaskFIFOAllocMeta rrmeta;

    virtual int selectIn( const port_key_t &name )
    {
        return rrmeta.selectIn( name );
    }

    virtual int selectOut( const port_key_t &name )
    {
        return rrmeta.selectOut( name );
    }

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const override
    {
        rrmeta.getPairIn( functor, fifo, selected );
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false ) override
    {
        UNUSED( is_oneshot ); /* RRWorkerMixAllocMeta is not for OneShotTask */
#if IGNORE_HINT_0CLONE
#else
        auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
        if( 0 == pi->runtime_info.nfifos )
        {
            return false;
            /* switch to OneShot since kernel has zero polling worker */
        }
#endif
        rrmeta.getPairOut( functor, fifo, selected, is_oneshot );
#if IGNORE_HINT_FULLQ
#else
        if( 0 == fifo->space_avail() )
        {
            return false;
        }
#endif
        return true;
    }

    virtual void nextFIFO( int selected ) override
    {
        rrmeta.nextFIFO( selected );
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
#if IGNORE_HINT_0CLONE
#else
        auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
        if( 0 == pi->runtime_info.nfifos )
        {
            return nullptr;
        }
#endif
        return rrmeta.wakeupConsumer( selected );
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
#if IGNORE_HINT_0CLONE
#else
        auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
        if( 0 == pi->runtime_info.nfifos )
        {
            return;
        }
#endif
        rrmeta.setConsumer( selected, worker );
    }

    virtual void invalidateOutputs() const override
    {
        rrmeta.invalidateOutputs();
    }

    virtual bool hasValidInput() const override
    {
        return rrmeta.hasValidInput();
    }

    virtual bool hasInputData( const port_key_t &name ) override
    {
        return rrmeta.hasInputData( name );
    }

    virtual bool isStatic() const override
    {
        return false;
    }

    /* consumerInit - allocate the consumers array, and also populate the
     * fifo_consumers map for AllocateMixCV
     * @param consumer - CondVarWorker*
     * @param fifo_consumers - std::unordered_map< FIFO*, CondVarWorker* > &
     */
    void consumerInit( CondVarWorker* consumer,
                       std::unordered_map< FIFO*,
                                           CondVarWorker* > &fifo_consumers )
    {
        rrmeta.consumerInit( consumer, fifo_consumers );
    }
};

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_MIXALLOCMETA_HPP */
