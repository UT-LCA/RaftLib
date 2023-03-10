/**
 * lambdak.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Wed Mar 08 17:10:36 2023
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
#ifndef RAFT_UTILS_LAMBDAK_TCC
#define RAFT_UTILS_LAMBDAK_TCC  1
#include <functional>
#include <utility>
#include <typeinfo>
#include <raft>

namespace raft
{
/** TODO, this needs some more error checking before production **/

/** pre-declare recursive struct / functions **/
template < class... PORTSL > struct AddPortsHelper;
template < class... PORTSK > struct AddSamePortsHelper;

constexpr auto default_pop = []( Task *task, bool dryrun ){ return true; };
constexpr auto default_alloc = []( Task *task, bool dryrun ){ return true; };

template < class... PORTS >
class lambdak : public raft::Kernel
{

public:
    typedef std::function< bool ( Task *task, bool dryrun ) > popfunc_t;
    typedef std::function< bool ( Task *task, bool dryrun ) > allocfunc_t;
    typedef std::function< raft::kstatus::value_t ( StreamingData &dataIn,
                                                    StreamingData &bufOut,
                                                    Task *task ) > compfunc_t;
    /**
     * constructor -
     * @param   inputs - const std::size_t number of inputs to the kernel
     * @param   outputs - const std::size_t number of outputs to the kernel
     * @param   func - static or lambda function to execute.
     */
    lambdak( const std::size_t inputs,
             const std::size_t outputs,
             compfunc_t compfunc,
             popfunc_t popfunc = default_pop,
             allocfunc_t allocfunc = default_alloc ) :
        raft::Kernel(), pop_func( popfunc ), alloc_func( allocfunc ),
        comp_func( compfunc )
    {
        add_ports< PORTS... >( inputs, outputs );
    }


    //FIXME, add copy constructor


    /**
     * compute - implement the compute function for this kernel
     */
    virtual raft::kstatus::value_t compute( StreamingData &dataIn,
                                            StreamingData &bufOut,
                                            Task *task )
    {
        return( comp_func( dataIn /** input streaming data **/,
                           bufOut /** output streaming buffer **/,
                           task /** execution task context **/ ) );
    }

    /**
     * pop - implement the pop function for this kernel
     */
    virtual bool pop( Task *task, bool dryrun )
    {
        return( pop_func( task, dryrun ) );
    }

    /**
     * allocate - implement the allocate function for this kernel
     */
    virtual bool allocate( Task *task, bool dryrun )
    {
        return( alloc_func( task, dryrun ) );
    }


private:
    /** lambda func passed by user **/
    popfunc_t pop_func;
    allocfunc_t alloc_func;
    compfunc_t comp_func;

    /** function **/
    template < class... PORTSM >
    void add_ports( const std::size_t input_max,
                    const std::size_t output_max )
    {
        const auto num_types( sizeof... (PORTSM) );
        if( num_types == 1 )
        {
            /** everybody gets same type, add here **/
            AddSamePortsHelper< PORTSM... >::add( input_max  /* count */,
                                                  output_max /* count */,
                                                  this );
        }
        /** no idea what type each port is, throw error **/
        else if( num_types != ( input_max + output_max ) )
        {
            /** TODO, make exception for here **/
            assert( false );
        }
        else /** num_types == ( input_max + output_max ) **/
        {
            /** multiple port type case **/
            std::size_t input_index(  0 );
            std::size_t output_index( 0 );
            AddPortsHelper< PORTSM... >::add( input_index,
                                              input_max,
                                              output_index,
                                              output_max,
                                              this );
        }
    }

}; /** end template lambdak **/

/** single class type, no recursion **/
template < class PORT, class... PORTSK >
struct AddSamePortsHelper< PORT, PORTSK... >
{
    static void add( const std::size_t input_count,
                     const std::size_t output_count,
                     Kernel *kernel )
    {
        using input_index_type = std::remove_const_t<decltype(input_count)>;
        for( input_index_type it( 0 ); it < input_count; it++ )
        {
            kernel->addInput< PORT >( std::to_string( it ) );
        }

        using output_index_type = std::remove_const_t<decltype(output_count)>;
        for( output_index_type it( 0 ); it < output_count; it++ )
        {
            kernel->addOutput< PORT >( std::to_string( it ) );
        }
    }
};

/** class recursion **/
template < class PORT, class... PORTSL >
struct AddPortsHelper< PORT, PORTSL... >
{
    static void add( std::size_t &input_index,
                     const std::size_t input_max,
                     std::size_t &output_index,
                     const std::size_t output_max,
                     Kernel *kernel )
    {
        if( input_index < input_max )
        {
            /** add ports in order, 0,1,2, etc. **/
            kernel->addInput< PORT >( std::to_string( input_index++ ) );
        }
        else if( output_index < output_max )
        {
            /** add ports in order, 0, 1, 2, etc. **/
            kernel->addOutput< PORT >( std::to_string( output_index++ ) );
        }
        else
        {
            /**
             * I think it'll be okay here simply to return, however
             * we might need the blank specialization below
             */
        }
        AddPortsHelper< PORTSL... >::add( input_index,
                                          input_max,
                                          output_index,
                                          output_max,
                                          kernel );
        return;
    }
};

template <>
struct AddPortsHelper<>
{
    static void add( std::size_t &input_index,
                     const std::size_t input_max,
                     std::size_t &output_index,
                     const std::size_t output_max,
                     Kernel *kernel )
    {
        UNUSED( input_index );
        UNUSED( input_max );
        UNUSED( output_index );
        UNUSED( output_max );
        UNUSED( kernel );
        return;
    }
};
//template < class... PORTS >
//void lambdak< PORTS... >::template add_ports_helper<>(
//        std::size_t & input_index,
//        const std::size_t input_max,
//        std::size_t & output_index,
//        const std::size_t output_max )
//{
//    UNUSED( input_index );
//    UNUSED( input_max );
//    UNUSED( output_index );
//    UNUSED( output_max );
//    return;
//}


} /* end namespace raft */
#endif /* END RAFT_UTILS_LAMBDAK_TCC */
