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

struct FIFOAllocMetaInterface
{
    /* selectIn - select an input port by name
     * @param name - const port_key_t &
     * @return int - selected input port index
     */
    virtual int selectIn( const port_key_t &name ) const = 0;

    /* selectOut - select an output port by name
     * @param name - const port_key_t &
     * @return int - selected output port index
     */
    virtual int selectOut( const port_key_t &name ) const = 0;

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
                             bool is_oneshot = false ) const = 0;

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

struct FIFOAllocMeta :
    public TaskAllocMeta, public virtual FIFOAllocMetaInterface
{
    FIFOAllocMeta() : TaskAllocMeta() {}
    virtual ~FIFOAllocMeta() = default;
};

/* KernelFIFOAllocMeta - Hold all read-only data for a kernel,
 * might be assigned to a task if no multiple fifos on any of the port
 */
struct KernelFIFOAllocMeta : public FIFOAllocMeta
{
    using name2port_map_t = std::unordered_map< port_key_t, int >;
    KernelFIFOAllocMeta( Kernel *k ) : FIFOAllocMetaInterface()
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

    virtual int selectIn( const port_key_t &name ) const
    {
        auto iter( name2port_in.find( name ) );
        assert( name2port_in.end() != iter );
        return iter->second;
    }

    virtual int selectOut( const port_key_t &name ) const
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
                             bool is_oneshot = false ) const
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
    RRTaskFIFOAllocMeta( const KernelFIFOAllocMeta &meta, int rr_idx ) :
        FIFOAllocMetaInterface(),
        kmeta( meta ),
        ninputs( meta.name2port_in.size() ),
        noutputs( meta.name2port_out.size() ),
        name2port_in( meta.name2port_in ),
        name2port_out( meta.name2port_out ),
        ports_in_info( meta.ports_in_info ),
        ports_out_info( meta.ports_out_info )
    {
        if( 0 < ninputs )
        {
            idxs_in = new int[ ninputs ]; /* offset */
            for( int i( 0 ); ninputs > i; ++i )
            {
                idxs_in[ i ] = rr_idx;
            }
            nclones = ports_in_info[ 0 ]->my_kernel->getCloneFactor();
        }
        else
        {
            idxs_in = nullptr;
        }
        if( 0 < noutputs )
        {
            idxs_out = new int[ noutputs << 1 ]; /* offset, base */
            idxs_out[ 1 ] = 0;
            for( int i( 0 ); noutputs > i; ++i )
            {
                idxs_out[ i << 1 ] = rr_idx;
                if( 0 == i )
                {
                    continue;
                }
                auto nfifos( ports_out_info[ i ]->runtime_info.nfifos );
                idxs_out[ ( i << 1 ) + 1 ] =
                    idxs_out[ ( i << 1 ) - 1 ] + nfifos;
            }
            nclones = ports_out_info[ 0 ]->my_kernel->getCloneFactor();
        }
        else
        {
            idxs_out = nullptr;
        }
        nclones = std::max( 1, nclones );
        consumers = nullptr;
    }

    virtual ~RRTaskFIFOAllocMeta()
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

    const KernelFIFOAllocMeta &kmeta;

    const int ninputs;
    const int noutputs;

    const KernelFIFOAllocMeta::name2port_map_t &name2port_in;
    const KernelFIFOAllocMeta::name2port_map_t &name2port_out;

    PortInfo ** ports_in_info;
    PortInfo ** ports_out_info;

    int *idxs_in;
    int *idxs_out;

    CondVarWorker **consumers;
    /* is flattened from 2-D array, serve as the cache of fifo_consumers */

    int nclones;

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
        auto fifo_idx( idxs_in[ selected ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ fifo_idx ];
    }

    virtual bool getPairOut( FIFOFunctor *&functor,
                             FIFO *&fifo,
                             int selected,
                             bool is_oneshot = false ) const
    {
        UNUSED( is_oneshot );
        auto *pi( ports_out_info[ selected ] );
        auto fifo_idx( idxs_out[ selected << 1 ] );
        functor = pi->runtime_info.fifo_functor;
        fifo = pi->runtime_info.fifos[ fifo_idx ];
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
        auto *pi( ports_out_info[ selected ] );
        auto nfifos( pi->runtime_info.nfifos );
        idxs_out[ selected << 1 ] += nclones;
        idxs_out[ selected << 1 ] %= nfifos;
    }

    virtual FIFO *wakeupConsumer( int selected ) const
    {
        auto idx( idxs_out[ selected << 1 ] +
                  idxs_out[ ( selected << 1 ) + 1 ] );
        // wake up the worker waiting for data
        if( nullptr != consumers[ idx ] )
        {
            consumers[ idx ]->wakeup();
            return nullptr;
        }
        auto *pi( ports_out_info[ selected ] );
        auto *fifos( pi->runtime_info.fifos );
        return fifos[ idxs_out[ selected << 1 ] ];
        /* let AllocateFIFOCV knows that it should look up fifo_consumers */
    }

    virtual void setConsumer( int selected, CondVarWorker *worker )
    {
        auto idx( idxs_out[ selected << 1 ] +
                  idxs_out[ ( selected << 1 ) + 1 ] );
        consumers[ idx ] = worker;
    }

    virtual void invalidateOutputs() const
    {
        if( 0 == noutputs )
        {
            return;
        }
        for( int i( 0 ); noutputs > i; ++i )
        {
            auto *pi( ports_out_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            auto idx_tmp( idxs_out[ i << 1 ] );
            do
            {
                fifos[ idx_tmp ]->invalidate();
                if( nullptr != consumers )
                {
                    auto idx( idx_tmp + idxs_out[ ( i << 1 ) + 1 ] );
                    if( nullptr != consumers[ idx ] )
                    {
                        consumers[ idx ]->wakeup();
                    }
                }
                idx_tmp += nclones;
                idx_tmp %= nfifos;
            } while( idx_tmp != idxs_out[ i << 1 ] );
        }
    }

    virtual bool hasValidInput() const
    {
        if( 0 == ninputs )
        {
            /* let the source polling worker loop until the stop signal */
            return true;
        }
        for( int i( 0 ); ninputs > i; ++i )
        {
            auto *pi( ports_in_info[ i ] );
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

    virtual bool hasInputData( const port_key_t &name )
    {
        if( 0 == ninputs )
        {
            /** only output ports, keep calling compute() till exits **/
            return true ;
        }

        int port_beg = 0, port_end = ninputs;
        if( null_port_value != name )
        {
            port_beg = name2port_in.at( name );
            port_end = port_beg + 1;
        }

        for( int i( port_beg ); port_end > i; ++i )
        {
            auto *pi( ports_in_info[ i ] );
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
        for( auto i( 0 ); ninputs > i; ++i )
        {
            auto *pi( ports_in_info[ i ] );
            auto *fifos( pi->runtime_info.fifos );
            auto nfifos( pi->runtime_info.nfifos );
            auto idx_tmp( idxs_in[ i ] );
            do
            {
                fifo_consumers[ fifos[ idx_tmp ] ] = consumer;
                idx_tmp += nclones;
                idx_tmp %= nfifos;
            } while( idx_tmp != idxs_in[ i ] );
        }

        if( 0 == noutputs )
        {
            return;
        }
        auto *last_port( ports_out_info[ noutputs - 1 ] );
        auto noutfifos( idxs_out[ ( noutputs << 1 ) + 1 ] +
                        last_port->runtime_info.nfifos );
        consumers = new CondVarWorker*[ noutfifos ]();
    }
};

} /** end namespace raft **/

#endif /* END RAFT_ALLOCATE_FIFOALLOCMETA_HPP */
