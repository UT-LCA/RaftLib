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

#define ARMQ_NO_HINT ( ARMQ_NO_HINT_FULLQ && ARMQ_NO_HINT_0CLONE )

namespace raft
{

struct BufListMetaAddOn
{
    BufListMetaAddOn( const KernelFIFOAllocMeta &meta ) : kmeta( meta )
    {
    }

    const KernelFIFOAllocMeta &kmeta;

    void pushOutBuf( DataRef &ref, int selected )
    {
        auto *node( new BufListNode() );
        node->pi = kmeta.ports_out_info[ selected ];
        auto *functor( node->pi->runtime_info.bullet_functor );
        node->ref = functor->allocate( ref );
        // insert into the linked list
        node->next = outbufs.next;
        outbufs.next = node;
    }

    DataRef allocateOutBuf( int selected )
    {
        auto *node( new BufListNode() );
        node->pi = kmeta.ports_out_info[ selected ];
        auto *functor( node->pi->runtime_info.bullet_functor );
        node->ref = functor->allocate();
        // insert into the linked list
        node->next = outbufs.next;
        outbufs.next = node;
        return node->ref;
    }

    bool popOutBuf( PortInfo *&pi_ptr, DataRef &ref, bool *is_last )
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


struct OneShotMixAllocMeta : public TaskAllocMeta, public BufListMetaAddOn
{
    OneShotMixAllocMeta( const KernelFIFOAllocMeta &meta ) :
        TaskAllocMeta(), BufListMetaAddOn( meta )
    {
    }

    virtual ~OneShotMixAllocMeta() = default;

    int selectIn( const port_key_t &name )
    {
        return kmeta.selectIn( name );
    }

    int selectOut( const port_key_t &name )
    {
        return kmeta.selectOut( name );
    }
};

struct TaskMixAllocMeta : public TaskFIFOAllocMeta, public BufListMetaAddOn
{
    TaskMixAllocMeta( const KernelFIFOAllocMeta &meta, int rr_idx ) :
        TaskFIFOAllocMeta( meta, rr_idx ), BufListMetaAddOn( meta )
    {
#if ! ARMQ_NO_HINT && ARMQ_DUMP_FIFO_STATS
        auto nfifos_out( nfifosOut() );
        if( 0 < nfifos_out )
        {
            oneshot_cnts = new uint64_t[ nfifos_out ]();
        }
        else
        {
            oneshot_cnts = nullptr;
        }
#endif
    }

    virtual ~TaskMixAllocMeta()
    {
#if ! ARMQ_NO_HINT && ARMQ_DUMP_FIFO_STATS
        if( nullptr != oneshot_cnts )
        {
            int nfifos_out( nfifosOut() );
            for( int idx( 0 ); nfifos_out > idx; ++idx )
            {
                std::cout << std::hex << (uint64_t)fifos_out[ idx ] <<
                    std::dec << " " << oneshot_cnts[ idx ] << std::endl;
            }
            delete[] oneshot_cnts;
            oneshot_cnts = nullptr;
        }
#endif
    }

    using TaskFIFOAllocMeta::kmeta;
#if ! ARMQ_NO_HINT && ARMQ_DUMP_FIFO_STATS
    uint64_t *oneshot_cnts;

    void oneshotCnt()
    {
        if( nullptr != oneshot_cnts )
        {
            oneshot_cnts[ idx_out_selected ]++;
        }
    }
#endif
};

struct RRWorkerMixAllocMeta : public TaskMixAllocMeta
{
    RRWorkerMixAllocMeta( const KernelFIFOAllocMeta &meta, int rr_idx ) :
        TaskMixAllocMeta( meta, rr_idx )
    {
    }

    /* getPairOut - get the functor and fifo of currently selected output port
     * @param functor - FIFOFunctor *&
     * @param fifo - FIFO *&
     * @return bool - true if output FIFO valid
     */
    bool getPairOut( FIFOFunctor *&functor,
                     FIFO *&fifo )
    {
#if ! ARMQ_NO_HINT_0CLONE
        if( 0 == port_out_selected->runtime_info.nfifos )
        {
            return false;
            /* switch to OneShot since kernel has zero polling worker */
        }
#endif
        fifo = fifos_out[ idx_out_selected ];
        functor = port_out_selected->runtime_info.fifo_functor;
#if ! ( ARMQ_DYNAMIC_ALLOC || ARMQ_NO_HINT_FULLQ )
        if( 0 == fifo->space_avail() )
        {
            return false;
        }
#endif
        return true;
    }

    /* nextFIFO - iterate to the next FIFO on the selected output port
     * @param selected - int
     */
    void nextFIFO( int selected )
    {
        /* to round-robin output FIFOs of the selected port */
        idx_out_selected = ++idxs_out[ selected << 1 ] +
                           idxs_out[ ( selected << 1 ) + 1 ];
        if( idxs_out[ ( selected << 1 ) + 3 ] <= idx_out_selected )
        {
            idxs_out[ selected << 1 ] = 0;
            idx_out_selected = idxs_out[ ( selected << 1 ) + 1 ];
        }
    }

};

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_MIXALLOCMETA_HPP */
