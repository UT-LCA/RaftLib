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

struct MixAllocMetaInterface : public FIFOAllocMetaInterface
{
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

struct MixAllocMeta :
    public TaskAllocMeta, public virtual MixAllocMetaInterface
{
    MixAllocMeta() : TaskAllocMeta() {}
    virtual ~MixAllocMeta() = default;
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

    virtual int selectIn( const port_key_t &name ) const
    {
        return kfifometa.selectIn( name );
    }

    virtual int selectOut( const port_key_t &name ) const
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
                             bool is_oneshot = false ) const
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

    virtual void getDrainPairOut( FIFOFunctor *&functor,
                                  FIFO *&fifo,
                                  int selected ) const
    {
        kfifometa.getDrainPairOut( functor, fifo, selected );
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
    OneShotMixAllocMeta( const KernelMixAllocMeta & meta ) :
        MixAllocMetaInterface(), kmeta( meta )
    {
    }

    virtual ~OneShotMixAllocMeta() = default;

    const KernelMixAllocMeta &kmeta;

    virtual int selectIn( const port_key_t &name ) const
    {
        return kmeta.selectIn( name );
    }

    virtual int selectOut( const port_key_t &name ) const
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
                             bool is_oneshot = false ) const
    {
        UNUSED( functor );
        UNUSED( fifo );
        UNUSED( selected );
        UNUSED( is_oneshot );
        return false; /* OneShotMixAllocMeta used by OneShotTask only */
    }

    virtual void getDrainPairOut( FIFOFunctor *&functor,
                                  FIFO *&fifo,
                                  int selected ) const
    {
        UNUSED( functor );
        UNUSED( fifo );
        UNUSED( selected );
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
    RRWorkerMixAllocMeta( const KernelMixAllocMeta &meta, int rr_idx ) :
        OneShotMixAllocMeta( meta ),
        noutputs( meta.kfifometa.name2port_out.size() )
    {
        auto ninputs( kmeta.kfifometa.name2port_in.size() );
        if( 0 < ninputs )
        {
            idxs_in = new int[ ninputs ];
            for( std::size_t i( 0 ); ninputs > i; ++i )
            {
                idxs_in[ i ] = rr_idx;
            }
            auto *pi( kmeta.kfifometa.ports_in_info[ 0 ] );
            nclones = pi->my_kernel->getCloneFactor();
        }
        else
        {
            idxs_in = nullptr;
        }

        if( 0 < noutputs )
        {
            auto *ports_out_info( kmeta.kfifometa.ports_out_info );
            idxs_out = new int[ noutputs << 1 ];
            idxs_out[ noutputs - 1 ] = 0;
            for( std::size_t i( 0 ); noutputs > i; ++i )
            {
                auto nfifos( ports_out_info[ i ]->runtime_info.nfifos );
                idxs_out[ i + noutputs ] =
                    idxs_out[ i + noutputs - 1 ] + nfifos;
                idxs_out[ i ] = rr_idx;
            }
            auto *pi( kmeta.kfifometa.ports_out_info[ 0 ] );
            nclones = pi->my_kernel->getCloneFactor();
        }
        else
        {
            idxs_out = nullptr;
        }
#if IGNORE_HINT_0CLONE
        nclones = std::max( 1, nclones );
#endif
        consumers = nullptr;
        fifo_consumers_ptr = nullptr;
    }

    virtual ~RRWorkerMixAllocMeta()
    {
        if( nullptr != idxs_in )
        {
            delete[] idxs_in;
        }
        if( nullptr != idxs_out )
        {
            delete[] idxs_out;
        }
    }

    const std::size_t noutputs;
    int *idxs_in;
    int *idxs_out;
    int nclones;

    CondVarWorker **consumers;
    /* is flattened from 2-D array, serve as the cache of fifo_consumers */
    std::unordered_map< FIFO*, CondVarWorker* >* fifo_consumers_ptr;

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const override
    {
        auto *pi( kmeta.kfifometa.ports_in_info[ selected ] );
        auto fifo_idx( idxs_in[ selected ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ fifo_idx ];
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false ) const override
    {
        UNUSED( is_oneshot ); /* RRWorkerMixAllocMeta is not for OneShotTask */
        auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
#if IGNORE_HINT_0CLONE
#else
        if( 0 == pi->runtime_info.nfifos )
        {
            return false;
            /* switch to OneShot since kernel has zero polling worker */
        }
#endif
        auto fifo_idx( idxs_out[ selected ] );
        fifo = pi->runtime_info.fifos[ fifo_idx ];
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

    virtual void nextFIFO( int selected ) override
    {
        /* to round-robin output FIFOs of the selected port */
        auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
        auto nfifos( pi->runtime_info.nfifos );
        idxs_out[ selected ] += nclones;
        idxs_out[ selected ] %= nfifos;
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
        auto idx( idxs_out[ selected ] +
                  idxs_out[ selected + noutputs ] );
        // wake up the worker waiting for data
        if( nullptr == consumers[ idx ] )
        {
            auto *pi( kmeta.kfifometa.ports_out_info[ selected ] );
            auto *fifos( pi->runtime_info.fifos );
            auto *fifo( fifos[ idxs_out[ selected ] ] );
            auto iter( fifo_consumers_ptr->find( fifo ) );
            if( fifo_consumers_ptr->end() != iter &&
                nullptr != iter->second )
            {
                consumers[ idx ] = iter->second; /* cache the lookup results */
            }
        }
        if( nullptr != consumers[ idx ] )
        {
            consumers[ idx ]->wakeup();
        }
        return nullptr;
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
        auto idx( idxs_out[ selected ] +
                  idxs_out[ selected + noutputs ] );
        consumers[ idx ] = worker;
    }

    virtual void invalidateOutputs() const override
    {
        if( 0 == noutputs )
        {
            return;
        }
        for( std::size_t i( 0 ); noutputs > i; ++i )
        {
            auto *pi( kmeta.kfifometa.ports_out_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
#if IGNORE_HINT_0CLONE
#else
            if( 0 == nfifos )
            {
                continue;
            }
#endif
            auto idx_tmp( idxs_out[ i ] );
            do
            {
                fifos[ idx_tmp ]->invalidate();
                if( nullptr != consumers )
                {
                    auto idx( idx_tmp + idxs_out[ i + noutputs ] );
                    if( nullptr != consumers[ idx ] )
                    {
                        consumers[ idx ]->wakeup();
                    }
                    else
                    {
                        /* might have never pushed on this fifo so the
                         * consumer is not cached yet, fall back to lookup
                         * the fifo_consumers map to make sure not missing
                         * a waiting consumer
                         */
                        auto iter =
                            fifo_consumers_ptr->find( fifos[ idx_tmp ] );
                        if( fifo_consumers_ptr->end() != iter &&
                            nullptr != iter->second )
                        {
                            iter->second->wakeup();
                        }
                    }
                }
                idx_tmp += nclones;
                idx_tmp %= nfifos;
            } while( idx_tmp != idxs_out[ i ] );
        }
    }

    virtual bool hasValidInput() const override
    {
        auto ninputs( kmeta.kfifometa.name2port_in.size() );
        if( 0 == ninputs )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }
        for( std::size_t i( 0 ); ninputs > i; ++i )
        {
            auto *pi( kmeta.kfifometa.ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            auto idx_tmp( idxs_in[ i ] );
            do
            {
                if( ! fifos[ idx_tmp ]->is_invalid() )
                {
                    return true;
                }
                idx_tmp += nclones;
                idx_tmp %= nfifos;
            } while( idx_tmp != idxs_in[ i ] );
        }
        return false;
    }

    virtual bool hasInputData( const port_key_t &name ) override
    {
        auto ninputs( kmeta.kfifometa.name2port_in.size() );
        if( 0 == ninputs )
        {
            /** only output ports, keep calling compute() till exits **/
            return true ;
        }

        int port_beg = 0, port_end = ninputs;
        if( null_port_value != name )
        {
            port_beg = kmeta.kfifometa.name2port_in.at( name );
            port_end = port_beg + 1;
        }

        for( int i( port_beg ); port_end > i; ++i )
        {
            auto *pi( kmeta.kfifometa.ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            auto idx_tmp( idxs_in[ i ] );
            do
            {
                const auto size( fifos[ idx_tmp ]->size() );
                if( 0 < size )
                {
                    idxs_in[ i ] = idx_tmp;
                    return true;
                }
                idx_tmp += nclones;
                idx_tmp %= nfifos;
            } while( idx_tmp != idxs_in[ i ] );
        }
        return false;
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
        auto ninputs( kmeta.kfifometa.name2port_in.size() );
        for( std::size_t i( 0 ); ninputs > i; ++i )
        {
            auto *pi( kmeta.kfifometa.ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            for( auto idx( consumer->clone_id ); nfifos > idx; idx += nclones )
            {
                fifo_consumers[ fifos[ idx ] ] = consumer;
            }
        }

        if( 0 == noutputs )
        {
            return;
        }
        auto *last_port( kmeta.kfifometa.ports_out_info[ noutputs - 1 ] );
        auto noutfifos( idxs_out[ ( noutputs << 1 ) - 1 ] +
                        last_port->runtime_info.nfifos );
        consumers = new CondVarWorker*[ noutfifos ]();
        fifo_consumers_ptr = &fifo_consumers;
    }
};

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_MIXALLOCMETA_HPP */
