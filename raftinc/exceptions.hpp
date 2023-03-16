/**
 * raftexception.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 15:31:44 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2016 Jonathan Beard
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
#ifndef RAFT_EXCEPTIONS_HPP
#define RAFT_EXCEPTIONS_HPP  1
#include <exception>
#include <cassert>
#include <cstring>
#include <string>

namespace raft
{

class RaftException : public std::exception
{
public:

    RaftException( const std::string message ) :
        message(
#if (defined _WIN64 ) || (defined _WIN32)
        /**
         * fix for warning C4996: 'strdup': The POSIX name for this item is
         * a bit annoying given it is a POSIX function, but this is the best
         * we can do, see: https://bit.ly/2TH0Jci
         */
        _strdup( message.c_str() )
#else //everybody else
        strdup( message.c_str() )
#endif
        )
    {
    }

    virtual const char* what() const noexcept
    {
        assert( message != nullptr );
        return( message );
    }
private:
    const char *message = nullptr;
};

/**
 * TO MAKE AN EXCEPTION
 * 1) MAKE AN EMPTY DERIVED CLASSS OF RaftException
 * 2) MAKE ONE OF THESE TEMPLATES WHICH DERIVES FROM THAT
 *    EMPTY CLASS
 * THEN ADD A using MYNEWEXCEPTION = Template< # >
 */

template < int N > class TemplateRaftException : public RaftException
{
public:
    TemplateRaftException( const std::string &message
                           ) : RaftException( message ) {}
};

/**
 * PortNotFoundException - throw me if there isn't a port by the
 * given name on the input nor output port.
 */
using PortNotFoundException
    = TemplateRaftException< 0 >;
/**
 * PortDoubleInitializeException - throw me if the port has been
 * doubly allocated somehow..this should likely be caught internal
 * to the runtime but worst case it gets thrown out to the user
 * to likely file a bug report with.
 */
using PortDoubleInitializeException
    = TemplateRaftException< 1 >;
/**
 * PortTypeMismatchException - use me if there is a port type mis-match
 * ports in raftlib are checked dynamically...given we could have
 * dynamic type conversion, we should allow this, however throw
 * this exception if the types are totally not compatible.
 */
using PortTypeMismatchException
    = TemplateRaftException< 2 >;
/**
 * AmbiguousPortAssignmentException - throw me if there is a link
 * without a specified name to a source or destination kernel that
 * has multiple potential mates for the link, hence ambiguous. These
 * links must have names.
 */
using AmbiguousPortAssignmentException
    = TemplateRaftException< 3 >;
/**
 * ClosedPortAccessException - Throw me if the user attempts an access
 * to a port that has been closed, i.e. if the upstream kernels are
 * not going to send anymore data then the port is closed. This could
 * happen for many different reasons though so there is another exception
 * to catch just the case of no more data.
 */
using ClosedPortAccessException
    = TemplateRaftException< 4 >;
/**
 * NoMoreDataException - case of no more data to be sent...if a user
 * tries to access the port when nothing more is coming then we throw
 * this exception.
 */
using NoMoreDataException
    = TemplateRaftException< 5 >;
/**
 * PortAlreadyExists - the port must exist only once, i.e. not conflict
 * for the input or output to a kernel. Therefore each prot name must
 * be unique for a given side, and if not this exception is thrown.
 */
using PortAlreadyExists
    = TemplateRaftException< 6 >;

/**
 * PortUnconnectedException - the port at the throwing kernel is not connected,
 * and therefore at execution time bad things will happen. Lets throw
 * an exception at runtime to catch this.
 */
using PortUnconnectedException
    = TemplateRaftException< 7 >;

/**
 * InvalidTopologyOperationException -
 */
using InvalidTopologyOperationException
    = TemplateRaftException< 8 >;

/**
 * NoSignalHandlerFoundException -
 */
using NoSignalHandlerFoundException
    = TemplateRaftException< 9 >;

/**
 * DataNotFoundException -
 */
using DataNotFoundException
    = TemplateRaftException< 10 >;

/**
 * MethodNotImplementedException -
 */
using MethodNotImplementdException
    = TemplateRaftException< 11 >;

} /** end namespace raft **/

#endif /* END RAFT_EXCEPTIONS_HPP */
