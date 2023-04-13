/**
 * rafttypes.hpp - some basic types defined under raft namespace
 * @author: Jonathan Beard
 * @version: Fri Sep 26 12:26:53 2014
 *
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
#ifndef RAFT_RAFTTYPES_HPP
#define RAFT_RAFTTYPES_HPP  1

#include <cstddef>
#include <cstdint>
#include <climits>
#include <limits>
#include <unordered_set>
#include <string>

#if (! defined STRING_NAMES) || (STRING_NAMES == 0)
#include "raftinc/hh.hpp"
#endif

namespace raft
{

using byte_t = std::uint8_t;
const static std::uint8_t bits_per_byte = CHAR_BIT;

/** predeclare raft::Kernel for kernelset_t below **/
class Kernel;
using kernelset_t = std::unordered_set< Kernel* >;

/** use this to turn on string names vs. off **/
/**
 * Notes: For port_key_type, there's the original string version
 * which turns out to be rather cumbersome for very small kernels,
 * and then there's the version that does hashing of strings then
 * uses the 64b hash of that string. There is a tiny change of port
 * name collision with the 64b version, however, it's incredibly
 * unlikely that this would occur.
 */
#if STRING_NAMES
    using port_name_t = std::string;
    using port_key_t = std::string;
    const static raft::port_key_t null_port_value = "";

#else
    /**
     * define the type of the port key, this is the value typed in by the
     * programmer to name ports. From the programmer perspective it'll look
     * like a string.
     */
    using port_name_t = highway_hash::easy_t;
    using port_key_t = highway_hash::hash_t::val_type;

    /**
     * just like the string, we need a value for uninitialized port
     * types.
     */
    const static raft::port_key_t null_port_value =
        std::numeric_limits< raft::port_key_t >::max();
    /**
     * set max length of the string for the fixed length representation
     * of the port name, will be used for debug only, doesn't really
     * constrain the length used by programmers when typing port names.
     */
    //const static std::uint32_t  port_name_max_length = 64;
    /**
     * fixed length name representation containing both the hash value
     * and the string name of the hash (although it is truncated to the
     * max selected length above.
     */
    //using port_key_t = highway_hash::data_fixed_t< raft::port_name_max_length >;

#endif

namespace order
{
enum value_t : std::uint8_t {
    in = 0,
    out,
    ORDER_N
};
} /** end namespace order **/

namespace trigger
{
/**
 * these are to enable the sub-kernel behavior where
 * the kernel specifies that all ports must be active
 * before firing the kernel...by "active" we mean that
 * all ports have some data.
 */
enum value_t : std::uint8_t {
    any_port = 0,
    all_port = 1
};
} /** end namespace trigger **/

namespace kstatus
{
enum value_t : std::uint8_t {
    stop,
    proceed
};
} /** end namespace kstatus **/

namespace signal
{
static const std::uint8_t MAX_SYSTEM_SIGNAL( 0xff );

enum value_t : std::uint8_t {
    none = 0,
    quit,
    term,
    eof = MAX_SYSTEM_SIGNAL
};
} /** end namespace signal **/

} /** end namespace raft **/


#if STRING_NAMES
static
raft::port_key_t
operator""_port( const char *input, std::size_t len )
{
    return( std::string( input ) );
}
#else
/**
 * use this to get a constexpr 64b unsigned hash
 * of a string. Must compile with C++17 or higher for
 * this to work given it involves std::array operators
 * in constexpr functions.
 * e.g. "foobar"_port, hashes the string at compile
 * time. Currently only g++ has this capability, maybe
 * the latest head of clang, apple clang does not.
 * The return type is a struct with the string,
 * the length, and the hash value. e.g.
 * auto data( "foobar"_port); then the field data.val
 * contains your hash.
 */
static
constexpr
highway_hash::easy_t
operator""_port( const char *input, std::size_t len )
{
    return( highway_hash::easy_t( input, len ) );
}
#endif

#endif /* END RAFT_RAFTTYPES_HPP */
