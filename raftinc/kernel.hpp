/**
 * kernel.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 15:22:24 2023
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
#ifndef RAFT_KERNEL_HPP
#define RAFT_KERNEL_HPP  1

#include <functional>
#include <utility>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <string>
#include <sstream>
#ifdef BENCHMARK
#include <atomic>
#endif
#include "exceptions.hpp"
#include "port_info.hpp"
#include "rafttypes.hpp"
#include "common.hpp"
#include "defs.hpp"
#include "kpair.hpp"
#include "task.hpp"


namespace raft {

class StreamingData;
class Kernel;

class Kernel
{

    /** some helper functions for port adding **/
    template < class T, class K, class P >
    void add_port_helper( K &kernel, P &port )
    {
        UNUSED( kernel );
        UNUSED( port );
    }
    
    template < class T, class K, class P, class PORTNAME, class... PORTNAMES >
    void add_port_helper( K &kernel,
                          P &port,
                          PORTNAME &&portname,
                          PORTNAMES&&... portnames )
    {
        kernel.template add_port< T >( port, portname );
        add_port_helper< T, K, P, PORTNAMES... >(
                kernel, port, std::forward< PORTNAMES >( portnames )... );
    }

public:
    /** default constructor **/
    Kernel() : kernel_id( kernel_count() )
    {
    }

    virtual ~Kernel() = default;

    /**
     * compute - function that programers should extended for the
     * actual computation.
     */
    virtual kstatus::value_t compute( StreamingData &data_in,
                                      StreamingData &data_out,
                                      Task *task ) = 0;

    /**
     * pop - function that programers should extended for
     * checking (w/ dryrun = true) and actually preparing the input.
     */
    virtual bool pop( Task *task, bool dryrun ) = 0;

    /**
     * allocate - function that programers should extended for
     * checking (w/ dryrun = true) and actually allocate the output
     * buffer.
     */
    virtual bool allocate( Task *task, bool dryrun ) = 0;

    /**
     * packInputData - function that packages and returns input data.
     */
    bool packInputData( Task *task )
    {
        return (this)->pop( task, false );
    }

    StreamingData &getInputData( Task *task )
    {
        (this)->pop( task, true );
        return task->getDataIn();
    }

    /**
     * addInput - wrap protected add_input method to expose it for the
     * helper classes used by lambdak, otherwise the kernel itself should
     * use add_input() instead
     */
    template< class T >
    void addInput( const port_name_t &name )
    {
        add_input< T >( name );
    }

    /**
     * addOutput - wrap protected add_output method to expose it for the
     * helper classes used by lambdak, otherwise the kernel itself should
     * use add_output() instead
     */
    template< class T >
    void addOutput( const port_name_t &name )
    {
        add_output< T >( name );
    }

    /**
     * packOutputBuf - function that allocate and packages the output
     * buffer.
     */
    bool packOutputBuf( Task *task )
    {
        return (this)->allocate( task, false );
    }

    StreamingData &getOutputBuf( Task *task )
    {
        (this)->allocate( task, true );
        return task->getBufOut();
    }

    std::size_t getId()
    {
        return( kernel_id );
    }

    /**
     * operator[] - returns the current kernel with the
     * specified port name enabled for linking.
     * @param portname - const raft::port_name_t&&
     * @return raft::kernel&&
     */
    KernelPort &operator []( const port_name_t &&portname )
    {
        auto *ptr( new KernelPort( this, portname ) );
        return( *ptr );
    }

    /**
     * k0 >> k1
     */
    Kpair& operator >> ( Kernel &rhs )
    {
        auto *ptr( new Kpair( this, &rhs ) );
        return( *ptr );
    }

    /**
     * k0 >> k1_ptr
     * k0 >> kernel_maker< K >()
     */
    Kpair& operator >> ( Kernel *rhs )
    {
        auto *ptr( new Kpair( this, rhs ) );
        return( *ptr );
    }

    /**
     * k0 >> k1["in0"]
     */
    Kpair& operator >> ( const KernelPort &rhs )
    {
        KernelPort *meta_ptr( new KernelPort( rhs ) );
        auto *ptr( new Kpair( this, *meta_ptr ) );
        return( *ptr );
    }

    /**
     * PORTS - input and output, use these to specify the connections
     * with other kernels.
     */
    using port_map_t = std::unordered_map< port_name_t, PortInfo >;
    port_map_t input;
    port_map_t output;

    bool allConnected() const
    {
        /**
         * NOTE: would normally have made this a part of the 
         * port class itself, however, for the purposes of 
         * delivering relevant error messages this is much
         * easier.
         */
        for( auto it( input.begin() ); it != input.end(); ++it )
        {
            /** 
             * this will work if this is a string or not, name, returns a 
             * type based on what is in defs.hpp.
             */
            const auto &port_name( it->first );
            const auto &port_info( it->second );
            /**
             * NOTE: with respect to the inputs, the 
             * other kernel is the source arc, the 
             * my kernel is the local kernel.
             */
            if( port_info.other_kernel == nullptr )
            {
                std::stringstream ss;
                ss << "Port from edge (" << "null" << " -> " << 
                    port_info.my_name << ") with kernel types (src: " << 
                    "nullptr" << "), (dst: " <<
                    common::printClassName( *port_info.my_kernel ) <<
                    "), exiting!!\n";
                throw PortUnconnectedException( ss.str() );
                    
            }
        }
    
        for( auto it( output.begin() ); it != output.end(); ++it )
        {
            const auto &port_name( it->first );
            const auto &port_info( it->second );
            /**
             * NOTE: with respect to the inputs, the 
             * other kernel is the source arc, the 
             * my kernel is the local kernel.
             */
            if( port_info.other_kernel == nullptr )
            {
                std::stringstream ss;
                ss << "Port from edge (" << port_info.my_name << " -> " << 
                    "null" << ") with kernel types (src: " << 
                    common::printClassName( *port_info.my_kernel ) <<
                    "), (dst: " << "nullptr" << "), exiting!!\n";
                throw PortUnconnectedException( ss.str() );
            }
        }
    }

    void setGroup( int g )
    {
        group_id = g;
    }

    const PortInfo &getInput( const port_name_t &name )
    {
        return get_port( (this)->input, name );
    }

    const PortInfo &getOutput( const port_name_t &name )
    {
        return get_port( (this)->output, name );
    }

    trigger::value_t sched_trigger = trigger::any_port;

protected:

    /** in namespace raft **/
    friend class DAG;

    /**
     * NOTE: doesn't need to be atomic since only one thread
     * per process will have responsibility to to create new
     * compute kernels, for multi-process, this is used in
     * conjunction with process identifier.
     */
    static std::size_t kernel_count( int inc = 1 )
    {
        static std::size_t cnt( 0 );
        cnt += inc;
        return cnt;
    }

    /**
     * add_input - adds and initializes one or multiple input ports
     * of the same data type for the name(s) given.
     * @param   portnames - port_name_t
     */
    template < class T, class... PORTNAMES >
    void add_input( PORTNAMES&&... portnames )
    {
        add_port_helper< T >( ( *this ), ( this )->input,
                std::forward< PORTNAMES >( portnames )... );
    }

    /**
     * add_output - adds and initializes one or multiple output ports
     * of the same data type for the name(s) given.
     * @param   portnames - port_name_t
     */
    template < class T, class... PORTNAMES >
    void add_output( PORTNAMES&&... portnames )
    {
        add_port_helper< T >( ( *this ), ( this )->output,
                std::forward< PORTNAMES >( portnames )... );
    }

    /**
     * get_port - get the port info with the given name.
     * Function throw an exception if no port found.
     * @param   port - port_map_t&
     * @param   port_name - const port_name_t&
     */
    static PortInfo &get_port( port_map_t &port,
                               const port_name_t &name )
    {
        if( null_port_value == name )
        {
            if( 0 == port.size() )
            {
                throw PortNotFoundException(
                        "At least one port must be defined" );
            }
            else if( 1 < port.size() )
            {
                throw AmbiguousPortAssignmentException(
                        "One port expected, more than one found!" );
            }
            return port.begin()->second;
        }
        else if( port.end() == port.find( name ) )
        {
            std::stringstream ss;
            ss << "Port not found for name \"" << name << "\"";
            throw PortNotFoundException( ss.str() );
        }
        return port[ name ];
    }

private:

    /**
     * add_port - adds and initializes a port for the name
     * given.  Function throw an exception if the port
     * already exists.
     * @param   port - port_map_t&
     * @param   port_name - const port_name_t&
     */
    template < class T >
    void add_port( port_map_t &port, const port_name_t &portname )
    {
        if( port.end() != port.find( portname ) )
        {
            std::stringstream ss;
            ss << "FATAL ERROR: port \"" << portname << "\" already exists!";
            throw PortAlreadyExists( ss.str() );
        }

        /**
         * we'll have to make a port info object first and pass it by copy
         * to the portmap.  Perhaps re-work later with pointers, but for
         * right now this will work and it doesn't necessarily have to
         * be performant since its only executed once.
         */
        PortInfo pi( typeid( T ) );
        pi.my_kernel = this;

        pi.my_name = portname;

        port.insert( std::make_pair( portname, pi ) );

        /**
         * sadly have to do the initialization for all runtimes structures
         * earlier here because T is available to the compiler only, later
         * runtime would have no way to retrive this, but capture T in
         * instantiated template class right now */
        port[ portname ].typeSpecificRuntimeInit< T >();

        return;
    }

    /**
     * set_port - populate the port info with the port info of another kernel
     * essentially bind two ports. Function throw an exception if the port
     * already connect.
     * @param   port - port_map_t&
     * @param   port_name - const port_name_t&
     * @param   other - const PortInfo&
     */
    static void set_port( port_map_t &port, const port_name_t &name,
                          const PortInfo &other )
    {
        PortInfo &p( port[ name ] );
        if( nullptr != p.other_kernel )
        {
            throw PortDoubleInitializeException( "Port double initialized" );
        }
        p.other_kernel = other.my_kernel;
        p.other_name = other.my_name;
        p.other_port = &other;
    }

    /**
     * set_input - wrapper of set_port for input ports.
     * Function throw an exception if the port already connect.
     * @param   port_name - const port_name_t&
     * @param   other - const PortInfo&
     */
    void set_input( const port_name_t &name, const PortInfo &other )
    {
        set_port( input, name, other );
    }

    /**
     * set_output - wrapper of set_port for output ports.
     * Function throw an exception if the port already connect.
     * @param   port_name - const port_name_t&
     * @param   other - const PortInfo&
     */
    void set_output( const port_name_t &name, const PortInfo &other )
    {
        set_port( output, name, other );
    }

    const std::size_t kernel_id;

    int group_id; /* for the result from partition */

}; /** end Kernel decl **/


template < class T /** kernel type **/,
           class ... Args >
T* kernel_maker( Args&&... params )
{
    auto *k( new T( std::forward< Args >( params )... ) );
    return k;
}


} /** end namespace raft */
#endif /* END RAFT_KERNEL_HPP */
