/**
 * signal.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 10:56:21 2023
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
#ifndef RAFTSIGNAL_HPP
#define RAFTSIGNAL_HPP  1
#include <cstddef>
#include "rafttypes.hpp"
/**
 * TODO, add templated signal so that the user
 * can define tuple-like structures based on
 * their own needs.
 */
namespace Buffer
{

using signal_t = raft::signal::value_t;

struct Signal
{
    Signal()
    {
    }
    Signal( const Signal &other )
    {
        (this)->sig = other.sig;
    }

    Signal& operator = ( signal_t signal )
    {
        (this)->sig = signal;
        return( (*this) );
    }
    Signal& operator = ( signal_t &signal )
    {
        (this)->sig = signal;
        return( (*this) );
    }

    operator signal_t ()
    {
        return( (this)->sig );
    }

    std::size_t getindex() noexcept
    {
        return( index );
    }

    signal_t sig = raft::signal::none;
    std::size_t index = 0;
};
} /** end namespace buffer **/
#endif /* END RAFTSIGNAL_HPP */
