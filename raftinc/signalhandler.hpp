/**
 * signalhandler.hpp - container for signal handlers,
 * also handles calling of the signal handler, assumes that
 * the handler has all the state it needs to execute.
 *
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar  7 12:39:13 2023
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
#ifndef RAFT_SIGNALHANDLER_HPP
#define RAFT_SIGNALHANDLER_HPP  1
#include <exception>
#include <unordered_map>
#include <string>
#include "rafttypes.hpp"

namespace raft
{

/**
 * forward declaration
 */
class Kernel;

class FIFO;

/**
 * sighandler - takes a kernel pointer and a data struct that is
 * reserved for whatever future use we or whomever thinks of.  It
 * can always be void, but whatever signal handler you add, it must
 * be able to take the input.
 * @param port   - FIFO&, fifo that sent the signal
 * @param kernel - raft::kernel* - kernel that fifo goes to
 * @param signal - const raft::signal - curr signal thrown
 * @param data   - void ptr
 */
using sighandler = kstatus::value_t (*)( FIFO                    &fifo,
                                         Kernel                *kernel,
                                         const signal::value_t  signal,
                                         void                    *data );

class SignalHandler
{
public:
    SignalHandler() = default;
    virtual ~SignalHandler() = default;

    /**
     * addHandler - adds the signal handler 'handler'
     * to this container.  If the handler already
     * exists for that signal then the last one to
     * get added supercedes it (i.e., we're relying
     * on the base class to be called first and derived
     * classes to potentially add newer handlers).
     * @param   signal - const raft::signal
     * @param   handler - sighandler
     */
    void addHandler( const signal::value_t signal,
                     sighandler handler )
    {
        handlers[ signal ] = handler;
    }

    /**
     * callHandler - calls the handler for the param signal,
     * an exception is thrown if the signal doesn't have a
     * handler and a sigterm is passed throughout the system.
     * @param   signal - const raft::signal
     * @param   fifo   - FIFO& current port that called the signal
     * @param   kernel - raft::kernel*, currently called kernel
     * @param   data   - void*
     * @returns raft::kstatus - returns whatever the handler says otherwise proceed
     * @throws  NoSignalHandlerFoundException
     */
    kstatus::value_t callHandler( const signal::value_t signal,
                                  FIFO &fifo,
                                  Kernel *kernel,
                                  void *data )
    {
        auto ret_func( handlers.find( signal ) );
        if( ret_func == handlers.end() )
        {
            throw NoSignalHandlerFoundException( "No handler found for: " +
                                                 std::to_string( signal ) );
        }
        else
        {
            return( ( *ret_func ).second( fifo,
                                          kernel,
                                          signal,
                                          data ) );
        }
    }

private:
    std::unordered_map< raft::signal::value_t, sighandler > handlers;
};

} /** end namespace raft **/
#endif /* END RAFT_SIGNALHANDLER_HPP */
