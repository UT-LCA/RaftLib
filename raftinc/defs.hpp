/**
 * defs.hpp -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tuw Mar  7 13:30:48 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2021 Jonathan Beard
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
#ifndef RAFT_DEFS_HPP
#define RAFT_DEFS_HPP  1
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <cstdint>
#include <vector>
#include <utility>
#include <bitset>
#include <memory>
#include <string>
#include <limits>

#ifndef INJECT_DEMANGLE_NAMESPACE
#define INJECT_DEMANGLE_NAMESPACE 1
#endif

#ifndef DEMANGLE_NAMESPACE
#define DEMANGLE_NAMESPACE raft
#endif

#ifndef INJECT_AFFINITY_NAMESPACE
#define INJECT_AFFINITY_NAMESPACE 1
#endif

#ifndef AFFINITY_NAMESPACE
#define AFFINITY_NAMESPACE raft
#endif

#ifndef UNUSED
#ifdef __clang__
#define UNUSED( x ) (void)(x)
#else
#define UNUSED( x )[&x]{}()
#endif
//FIXME need to double check to see IF THIS WORKS ON MSVC
#endif

#ifndef L1D_CACHE_LINE_SIZE

#ifdef _MSC_VER
#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)
#define WARNING(desc) message(__FILE__ "(" STRINGIZE(__LINE__) ") : Warning: " #desc)
#pragma WARNING(Using 64 bytes as default cache line size, to fix recompile with -DL1D_CACHE_LINE_SIZE=XXX)
#else
#warning "Using 64 bytes as default cache line size, to fix recompile with -DL1D_CACHE_LINE_SIZE=XXX"
#endif

#define L1D_CACHE_LINE_SIZE 64

#endif

/**
 * Note: there is a NICE define that can be uncommented
 * below if you want sched_yield called when waiting for
 * writes or blocking for space, otherwise blocking will
 * actively spin while waiting.
 */
#define NICE 1

template < typename T >
    using set_t = std::set< T >;

using ptr_set_t = set_t< std::uintptr_t >;
using recyclefunc_t = std::function< void( void * ) >;
using ptr_map_t = std::map< std::uintptr_t, recyclefunc_t >;
using ptr_t = std::uintptr_t;

using core_id_t = std::int64_t;


#endif /* END RAFT_DEFS_HPP */

/**
 * internaldefs.hpp - cross-platform align and unlikely defines
 * @author: Jonathan Beard
 * @version: Mon Apr  3 04:49:41 2017
 *
 * Copyright 2017 Jonathan Beard
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
#ifndef RAFTINTERNALDEFS_HPP
#define RAFTINTERNALDEFS_HPP  1

#ifdef _MSC_VER
#    if (_MSC_VER >= 1800)
#        define __alignas_is_defined 1
#    endif
#    if (_MSC_VER >= 1900)
#        define __alignof_is_defined 1
#    endif
#else
#    ifndef __APPLE__
#    include <cstdalign>   // __alignas/of_is_defined directly from the implementation
#    endif
#endif

/**
 * should be included, but just in case there are some
 * compilers with only experimental C++11 support still
 * running around, check macro..turns out it's #ifdef out
 * on GNU G++ so checking __cplusplus flag as indicated
 * by https://goo.gl/JD4Gng
 */
#if ( __alignas_is_defined == 1 ) || ( __cplusplus >= 201103L )
#    define ALIGN(X) alignas(X)
#else
#    pragma message("C++11 alignas unsupported :( Falling back to compiler attributes")
#    ifdef __GNUG__
#        define ALIGN(X) __attribute__ ((aligned(X)))
#    elif defined(_MSC_VER)
#        define ALIGN(X) __declspec(align(X))
#    else
#        error Unknown compiler, unknown alignment attribute!
#    endif
#endif

#if ( __alignas_is_defined == 1 ) || ( __cplusplus >= 201103L )
#    define ALIGNOF(X) alignof(x)
#else
#    pragma message("C++11 alignof unsupported :( Falling back to compiler attributes")
#    ifdef __GNUG__
#        define ALIGNOF(X) __alignof__ (X)
#    elif defined(_MSC_VER)
#        define ALIGNOF(X) __alignof(X)
#    else
#        error Unknown compiler, unknown alignment attribute!
#    endif
#endif


#if (defined __linux) || (defined __APPLE__ )

#define R_LIKELY( var ) __builtin_expect( var, 1 )
#define R_UNLIKELY( var ) __builtin_expect( var, 0 )

#else

#define R_LIKELY( var ) var
#define R_UNLIKELY( var ) var

#endif

#endif /* END RAFTINTERNALDEFS_HPP */
