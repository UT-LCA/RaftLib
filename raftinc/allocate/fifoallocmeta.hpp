/**
 * fifoallocmeta.hpp - the meta data structures used by FIFO-based
 * allocater classes.
 * @author: Qinzhe Wu
 * @version: Wed Apr 05 13:43:06 2023
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
#ifndef RAFT_ALLOCATE_FIFOALLOCMETA_HPP
#define RAFT_ALLOCATE_FIFOALLOCMETA_HPP  1

#include <numeric>
#include <unordered_set>
#include <unordered_map>

#if UT_FOUND
#include <ut>
#endif

#include "raftinc/defs.hpp"
#include "raftinc/dag.hpp"
#include "raftinc/kernel.hpp"
#include "raftinc/port_info.hpp"
#include "raftinc/exceptions.hpp"
#include "raftinc/pollingworker.hpp"
#include "raftinc/oneshottask.hpp"
#include "raftinc/allocate/allocate.hpp"
#include "raftinc/allocate/fifo.hpp"
#include "raftinc/allocate/ringbuffer.tcc"
#include "raftinc/allocate/buffer/buffertypes.hpp"

/**
 * ALLOC_ALIGN_WIDTH - in previous versions we'd align based
 * on perceived vector width, however, there's more benefit
 * in aligning to cache line sizes.
 */

#if defined __AVX__ || __AVX2__ || _WIN64
#define ALLOC_ALIGN_WIDTH L1D_CACHE_LINE_SIZE
#else
#define ALLOC_ALIGN_WIDTH L1D_CACHE_LINE_SIZE
#endif

#define INITIAL_ALLOC_SIZE 64

namespace raft
{

struct FIFOAllocMeta : public TaskAllocMeta
{
    FIFOAllocMeta() : TaskAllocMeta() {}
    virtual ~FIFOAllocMeta() = default;

    /* selectIn - select an input port by name
     * @param name - const port_key_t &
     * @return int - selected input port index
     */
    virtual int selectIn( const port_key_t &name ) = 0;

    /* selectOut - select an output port by name
     * @param name - const port_key_t &
     * @return int - selected output port index
     */
    virtual int selectOut( const port_key_t &name ) = 0;

    /* getPortsInInfo - get the port info of input port
     * @return PortInfo*
     */
    virtual PortInfo **getPortsInInfo() const = 0;

    /* getPortsOutInfo - get the port info of output ports
     * @return PortInfo*
     */
    virtual PortInfo **getPortsOutInfo() const = 0;

    /* getPairIn - get the functor and fifo of currently selected input port
     * @param functor - FIFOFunctor *&
     * @param fifo - FIFO *&
     * @param selected - int
     */
    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const = 0;

    /* getPairOut - get the functor and fifo of currently selected output port
     * @param functor - FIFOFunctor *&
     * @param fifo - FIFO *&
     * @param selected - int
     * @param is_oneshot - int
     * @return bool - true if output FIFO valid
     */
    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false ) = 0;

    /* getDrainPairOut - get the functor and fifo of currently
     * output port being drain, used by OneShotTask
     * @param functor - FIFOFunctor *&
     * @param fifo - FIFO *&
     * @param selected - int
     */
    virtual void getDrainPairOut( FIFOFunctor *&functor,
                                  FIFO *&fifo,
                                  int selected ) const = 0;

    /* nextFIFO - iterate to the next FIFO on the selected output port
     * @param selected - int
     */
    virtual void nextFIFO( int selected ) = 0;

    /* wakeupConsumer - wakeup the consumer on the selected output FIFO,
     * used by AllocateFIFOCV after push/send
     * @param selected - int
     * @return FIFO * - nullptr indicates a successful wakeup, otherwise
     *                  it returns the FIFO pointer missing consumer info
     *                  so AllocateFIFOCV could lookup fifo_consumers to
     *                  populate the consumers[]
     */
    virtual FIFO *wakeupConsumer( int selected ) const = 0;

    /* setConsumer - set the consumer on the selected output FIFO
     * @param selected - int
     * @param CondVarWorker * - worker
     */
    virtual void setConsumer( int selected, CondVarWorker *worker ) = 0;

    /* invalidateOutputs - invalidate all the output ports */
    virtual void invalidateOutputs() const = 0;

    /* hasValidInput - check whether there is still valid input port */
    virtual bool hasValidInput() const = 0;

    /* hasInputData - check whether there is any input data */
    virtual bool hasInputData( const port_key_t &name ) = 0;

    /* isStatic - is this alloc meta static */
    virtual bool isStatic() const = 0;
};

/* KernelFIFOAllocMeta - Hold all read-only data for a kernel,
 * might be assigned to a task if no multiple fifos on any of the port
 */
struct KernelFIFOAllocMeta : public FIFOAllocMeta
{
    using name2port_map_t = std::unordered_map< port_key_t, int >;
    KernelFIFOAllocMeta( Kernel *k ) : FIFOAllocMeta()
    {
        ports_in_info = new PortInfo*[ k->input.size() ];
        ports_out_info = new PortInfo*[ k->output.size() ];
        name2port_in.reserve( k->input.size() );
        name2port_out.reserve( k->output.size() );

        nosharers = ( 1 >= k->getCloneFactor() );

        int idx = 0;
        for( auto &p : k->input )
        {
            name2port_in.emplace( p.first, idx );
            ports_in_info[ idx++ ] = &p.second;
            if( 1 < p.second.runtime_info.nfifos )
            {
                nosharers = false;
            }
        }
        idx = 0;
        for( auto &p : k->output )
        {
            name2port_out.emplace( p.first, idx );
            ports_out_info[ idx++ ] = &p.second;
            if( 1 < p.second.runtime_info.nfifos )
            {
                nosharers = false;
            }
        }
    }

    virtual ~KernelFIFOAllocMeta()
    {
        delete[] ports_in_info;
        delete[] ports_out_info;
    }

    /* this is the very one place that translate port_key_t into int,
     * so that we can index array in all meta structure,
     * rather than lookup map by port_key_t every time */
    name2port_map_t name2port_in;
    name2port_map_t name2port_out;

    /* rearrange Kernel::input/output from map to array */
    PortInfo **ports_in_info;
    PortInfo **ports_out_info;

    bool nosharers;

    virtual int selectIn( const port_key_t &name )
    {
        auto iter( name2port_in.find( name ) );
        assert( name2port_in.end() != iter );
        return iter->second;
    }

    virtual int selectOut( const port_key_t &name )
    {
        auto iter( name2port_out.find( name ) );
        assert( name2port_out.end() != iter );
        return iter->second;
    }

    virtual PortInfo **getPortsInInfo() const
    {
        return ports_in_info;
    }

    virtual PortInfo **getPortsOutInfo() const
    {
        return ports_out_info;
    }

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const
    {
        auto *pi( ports_in_info[ selected ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ 0 ];
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false )
    {
        auto *pi( ports_out_info[ selected ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ is_oneshot ? 1 : 0 ];
        return true;
    }

    virtual void getDrainPairOut( FIFOFunctor *&functor,
                                  FIFO *&fifo,
                                  int selected ) const
    {
        auto *pi( ports_out_info[ selected ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ 1 ];
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
        auto noutputs( name2port_out.size() );
        for( std::size_t i( 0 ); noutputs > i; ++i )
        {
            auto *pi( ports_out_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            fifos[ 0 ]->invalidate();
        }
    }

    virtual bool hasValidInput() const
    {
        auto ninputs( name2port_in.size() );
        if( 0 == ninputs )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }
        for( std::size_t i( 0 ); ninputs > i; ++i )
        {
            auto *pi( ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            if( ! fifos[ 0 ]->is_invalid() )
            {
                return true;
            }
        }
        return false;
    }

    virtual bool hasInputData( const port_key_t &name )
    {
        auto ninputs( name2port_in.size() );

        if( 0 == ninputs )
        {
            /** only output ports, keep calling compute() till exits **/
            return true ;
        }

        std::size_t port_beg = 0, port_end = ninputs;
        if( null_port_value != name )
        {
            port_beg = name2port_in.at( name );
            port_end = port_beg + 1;
        }

        for( std::size_t i( port_beg ); port_end > i; ++i )
        {
            auto *pi( ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            for( int idx_tmp( 0 ); nfifos > idx_tmp; ++idx_tmp )
            {
                if( 0 < fifos[ idx_tmp ]->size() )
                {
                    return true;
                }
            }
        }
        return false;
    }

    virtual bool isStatic() const
    {
        return true;
    }
};

/* RRTaskFIFOAllocMeta - used by PollingWorker having multiple FIFOs of an
 * port and would iterate in the Round-Robin manner.
 */
struct RRTaskFIFOAllocMeta : public FIFOAllocMeta
{
    RRTaskFIFOAllocMeta( KernelFIFOAllocMeta &meta, int rr_idx ) :
        FIFOAllocMeta(),
        kmeta( meta ),
        ninputs( meta.name2port_in.size() ),
        noutputs( meta.name2port_out.size() ),
        ports_in_info( meta.ports_in_info ),
        ports_out_info( meta.ports_out_info )
    {
        int nclones = 1;
        if( 0 < ninputs )
        {
            nclones =
                std::max( nclones,
                          ports_in_info[ 0 ]->my_kernel->getCloneFactor() );

            idxs_in = new int[ ninputs + 1 ]; /* base */
            idxs_in[ 0 ] = 0;
            for( std::size_t i( 0 ); ninputs > i; ++i )
            {
                /* calculate the number of fifos for this worker */
                auto *pi( ports_in_info[ i ] );
                auto nfifos( pi->runtime_info.nfifos );
                auto fifo_share( nfifos / nclones +
                                 ( rr_idx < ( nfifos % nclones ) ? 1 : 0 ) );
                idxs_in[ i + 1 ] = idxs_in[ i ] + fifo_share;
            }
            /* allocate and populating fifo array */
            fifos_in = new FIFO*[ idxs_in[ ninputs ] ];
            std::size_t idx_beg = 0;
            std::size_t idx_end;
            for( std::size_t i( 0 ); ninputs > i; ++i )
            {
                idx_end = idxs_in[ i + 1 ];
                auto *fifos( ports_in_info[ i ]->runtime_info.fifos );
                for( std::size_t idx( idx_beg ); idx_end > idx; ++idx )
                {
                    fifos_in[ idx ] =
                        fifos[ ( idx - idx_beg ) * nclones + rr_idx ];
                }
                idx_beg = idx_end;
            }

            port_in_selected = ports_in_info[ 0 ];
            fifo_in_selected = fifos_in[ 0 ];
        }
        else
        {
            idxs_in = nullptr;
            fifos_in = nullptr;
            port_in_selected = nullptr;
            fifo_in_selected = nullptr;
        }
        if( 0 < noutputs )
        {
            nclones =
                std::max( nclones,
                          ports_out_info[ 0 ]->my_kernel->getCloneFactor() );

            idxs_out = new int[ ( noutputs + 1 ) << 1 ]; /* offset, base */
            idxs_out[ 1 ] = 0;
            for( std::size_t i( 0 ); noutputs > i; ++i )
            {
                idxs_out[ i << 1 ] = 0;
                /* calculate the number of fifos for this worker */
                auto *pi( ports_out_info[ i ] );
                auto nfifos( pi->runtime_info.nfifos );
                auto fifo_share( nfifos / nclones +
                                 ( rr_idx < ( nfifos % nclones ) ? 1 : 0 ) );
                idxs_out[ ( i << 1 ) + 3 ] =
                    idxs_out[ ( i << 1 ) + 1 ] + fifo_share;
            }
            if( 0 < idxs_out[ ( noutputs << 1 ) + 1 ] )
            {
                /* allocate and populating fifo array */
                fifos_out = new FIFO*[ idxs_out[ ( noutputs << 1 ) + 1 ] ];
                std::size_t idx_beg = 0;
                std::size_t idx_end;
                for( std::size_t i( 0 ); noutputs > i; ++i )
                {
                    idx_end = idxs_out[ ( i << 1 ) + 3 ];
                    auto *fifos( ports_out_info[ i ]->runtime_info.fifos );
                    for( std::size_t idx( idx_beg ); idx_end > idx; ++idx )
                    {
                        fifos_out[ idx ] =
                            fifos[ ( idx - idx_beg ) * nclones + rr_idx ];
                    }
                    idx_beg = idx_end;
                }
                port_out_selected = ports_out_info[ 0 ];
                idx_out_selected = 0;
            }
            else /* outputs could be all 0 clone */
            {
                fifos_out = nullptr;
                port_out_selected = nullptr;
                idx_out_selected = 0;
            }
        }
        else
        {
            idxs_out = nullptr;
            fifos_out = nullptr;
            port_out_selected = nullptr;
            idx_out_selected = 0;
        }
        consumers = nullptr;
        fifo_consumers_ptr = nullptr;
    }

    virtual ~RRTaskFIFOAllocMeta()
    {
        if( nullptr != idxs_in )
        {
            delete[] idxs_in;
            idxs_in = nullptr;
            delete[] fifos_in;
            fifos_in = nullptr;
        }
        if( nullptr != idxs_out )
        {
            delete[] idxs_out;
            idxs_out = nullptr;
            delete[] fifos_out;
            fifos_out = nullptr;
        }
        if( nullptr != consumers )
        {
            delete[] consumers;
            consumers = nullptr;
        }
    }

    KernelFIFOAllocMeta &kmeta;

    const std::size_t ninputs;
    const std::size_t noutputs;

    PortInfo ** ports_in_info;
    PortInfo ** ports_out_info;

    int *idxs_in;
    int *idxs_out;

    /* the arrays bellow are flattend from 2-D array */
    FIFO **fifos_in;
    FIFO **fifos_out;
    CondVarWorker **consumers; /* serve as the cache of fifo_consumers */
    std::unordered_map< FIFO*, CondVarWorker* >* fifo_consumers_ptr;

    /* cache for selection */
    PortInfo *port_in_selected;
    PortInfo *port_out_selected;
    FIFO *fifo_in_selected;
    int idx_out_selected;

    virtual int selectIn( const port_key_t &name )
    {
        auto selected( kmeta.selectIn( name ) );
        port_in_selected = ports_in_info[ selected ];
        //TODO: Round-robin the input fifos?
        fifo_in_selected = fifos_in[ idxs_in[ selected ] ];
        return selected;
    }

    virtual int selectOut( const port_key_t &name )
    {
        auto selected( kmeta.selectOut( name ) );
        port_out_selected = ports_out_info[ selected ];
        idx_out_selected = idxs_out[ selected << 1 ] +
                           idxs_out[ ( selected << 1 ) + 1 ];
        return selected;
    }

    virtual PortInfo **getPortsInInfo() const
    {
        return ports_in_info;
    }

    virtual PortInfo **getPortsOutInfo() const
    {
        return ports_out_info;
    }

    virtual void getPairIn( FIFOFunctor *&functor,
                            FIFO *&fifo,
                            int selected ) const
    {
        UNUSED( selected );
        fifo = fifo_in_selected;
        functor = port_in_selected->runtime_info.fifo_functor;
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false )
    {
        UNUSED( is_oneshot );
        UNUSED( selected );
        fifo = fifos_out[ idx_out_selected ];
        functor = port_out_selected->runtime_info.fifo_functor;
        return true;
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
        /* to round-robin output FIFOs of the selected port */
        if( idxs_out[ ( selected << 1 ) + 3 ] <= ++idxs_out[ selected << 1 ] )
        {
            idxs_out[ selected << 1 ] = idxs_out[ ( selected << 1 ) + 1 ];
        }
        idx_out_selected = idxs_out[ selected << 1 ] +
                           idxs_out[ ( selected << 1 ) + 1 ];
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
        UNUSED( selected );
        // wake up the worker waiting for data
        if( nullptr != consumers[ idx_out_selected ] )
        {
            consumers[ idx_out_selected ]->wakeup();
            return nullptr;
        }
        auto iter( fifo_consumers_ptr->find(
                    fifos_out[ idx_out_selected ] ) );
        if( fifo_consumers_ptr->end() != iter &&
            nullptr != iter->second )
        {
            consumers[ idx_out_selected ] = iter->second;
            /* cache the lookup results */
            consumers[ idx_out_selected ]->wakeup();
        }
        return nullptr;
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
        consumers[ idx_out_selected ] = worker;
    }

    virtual void invalidateOutputs() const
    {
        if( 0 == noutputs )
        {
            return;
        }
        std::size_t nfifos_out( idxs_out[ ( noutputs << 1 ) + 1 ] );
        for( std::size_t i( 0 ); nfifos_out > i; ++i )
        {
            fifos_out[ i ]->invalidate();
        }
        if( nullptr != consumers )
        {
            for( std::size_t i( 0 ); nfifos_out > i; ++i )
            {
                if( nullptr != consumers[ i ] )
                {
                    consumers[ i ]->wakeup();
                }
                else
                {
                    /* might have never pushed on this fifo so the
                     * consumer is not cached yet, fall back to lookup
                     * the fifo_consumers map to make sure not missing
                     * a waiting consumer
                     */
                    auto iter = fifo_consumers_ptr->find( fifos_out[ i ] );
                    if( fifo_consumers_ptr->end() != iter &&
                        nullptr != iter->second )
                    {
                        iter->second->wakeup();
                    }
                }
            }
        }
    }

    virtual bool hasValidInput() const
    {
        if( 0 == ninputs )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }
        std::size_t nfifos_in( idxs_in[ ninputs ] );
        for( std::size_t i( 0 ); nfifos_in > i; ++i )
        {
            if( ! fifos_in[ i ]->is_invalid() )
            {
                return true;
            }
        }
        return false;
    }

    virtual bool hasInputData( const port_key_t &name )
    {
        if( 0 == ninputs )
        {
            /** only output ports, keep calling compute() till exits **/
            return true ;
        }

        int idx_beg = 0, idx_end = idxs_in[ ninputs ];
        std::size_t port_idx = 1;
        if( null_port_value != name )
        {
            int selected = kmeta.selectIn( name );
            idx_beg = idxs_in[ selected ];
            idx_end = idxs_in[ selected + 1 ];
            port_idx = selected + 1;
        }

        for( int idx( idx_beg ); idx_end > idx; ++idx )
        {
            if( idxs_in[ port_idx ] <= idx )
            {
                port_idx++;
            }
            const auto size( fifos_in[ idx ]->size() );
            if( 0 < size )
            {
                port_in_selected = ports_in_info[ port_idx - 1 ];
                fifo_in_selected = fifos_in[ idx ];
                return true;
            }
        }
        return false;
    }

    virtual bool isStatic() const
    {
        return false;
    }

    /* consumerInit - allocate the consumers array, and also populate the
     * fifo_consumers map for AllocateFIFOCV
     * @param consumer - CondVarWorker*
     * @param fifo_consumers - std::unordered_map< FIFO*, CondVarWorker* > &
     */
    void consumerInit( CondVarWorker* consumer,
                       std::unordered_map< FIFO*,
                                           CondVarWorker* > &fifo_consumers )
    {
        if( 0 < ninputs )
        {
            std::size_t nfifos_in( idxs_in[ ninputs ] );
            for( std::size_t i( 0 ); nfifos_in > i; ++i )
            {
                fifo_consumers[ fifos_in[ i ] ] = consumer;
            }
        }

        if( 0 == noutputs )
        {
            return;
        }

        std::size_t nfifos_out( idxs_out[ ( noutputs << 1 ) + 1 ] );
        consumers = new CondVarWorker*[ nfifos_out ]();
        fifo_consumers_ptr = &fifo_consumers;
    }
};

/* GreedyTaskFIFOAllocMeta - used by CondVarWorker having multiple FIFOs of an
 * port and would iterate in the greedy manner.
 */
struct GreedyTaskFIFOAllocMeta : public RRTaskFIFOAllocMeta
{
    GreedyTaskFIFOAllocMeta( KernelFIFOAllocMeta &meta, int idx ) :
        RRTaskFIFOAllocMeta( meta, idx )
    {
    }

    virtual ~GreedyTaskFIFOAllocMeta() = default;

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false )
    {
        UNUSED( is_oneshot );
        functor = ports_out_info[ selected ]->runtime_info.fifo_functor;
        for( auto idx( idxs_out[ ( selected << 1 ) + 1 ] );
             idxs_out[ ( selected << 1 ) + 3 ] > idx; idx++ )
        {
            if( fifos_out[ idx ]->space_avail() )
            {
                fifo = fifos_out[ idx ];
                idx_out_selected = idx;
                return true;
            }
        }
        fifo = fifos_out[ 0 ];
        idx_out_selected = idxs_out[ ( selected << 1 ) + 1 ];
        return true;
    }

    virtual void nextFIFO( int selected )
    {
        UNUSED( selected );
    }
};

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_FIFOALLOCMETA_HPP */
