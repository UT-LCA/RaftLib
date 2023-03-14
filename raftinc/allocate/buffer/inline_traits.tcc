#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include <typeinfo>

#include "raftinc/defs.hpp"

#ifndef RAFT_ALLOCATE_BUFFER_INLINE_TRAITS_TCC
#define RAFT_ALLOCATE_BUFFER_INLINE_TRAITS_TCC 1

namespace Buffer
{
/**
 * TODO, make a class to abstract figuring out the
 * move/copy/allocate semantics...it's getting far
 * go crowed in both the fifo.hpp and ringbufferheap.tcc
 * files, and therefore confusing.
 */

//FIXME, add std::move for int_allocate obj
/**
 * see if the given class/type fits within a single cache line
 */
template < class T >
struct fits_in_cache_line :
   std::integral_constant< bool, ( sizeof( T ) <= L1D_CACHE_LINE_SIZE ) >{};

/**
 * NOW FOR ALLOC CHECKS
 */

/**
 * inline_class_alloc, true if less than cache and has a ctor
 */
template < class T >
struct inline_class_alloc : std::integral_constant< bool,
         fits_in_cache_line< T >::value &&
         std::is_class< T >::value >{};

template < class T >
using inline_class_alloc_t =
    typename std::enable_if< inline_class_alloc< T >::value >::type;


/** cover the other two cases for non-ctor mem alloc **/
template < class T >
struct inline_nonclass_alloc : std::integral_constant< bool,
      fits_in_cache_line< T >::value &&
      ( std::is_array< T >::value || std::is_fundamental< T >::value ) >{};

template < class T >
using inline_nonclass_alloc_t =
    typename std::enable_if< inline_nonclass_alloc< T >::value >::type;


/** cover both inline cases **/
template < class T >
struct inline_alloc : std::integral_constant< bool,
    inline_class_alloc< T >::value || inline_nonclass_alloc< T >::value >{};

template < class T >
using inline_alloc_t =
    typename std::enable_if< inline_alloc< T >::value >::type;


/** covers classes & constructables that are bigger than cache **/
template < class T >
struct ext_class_alloc : std::integral_constant< bool,
         ! fits_in_cache_line< T >::value &&
         std::is_class< T >::value >{};

/** designed for array types **/
template < class T >
struct ext_mem_alloc : std::integral_constant< bool,
         ! fits_in_cache_line< T >::value &&
         std::is_array< T >::value >{};

template < class T >
struct ext_alloc : std::integral_constant< bool,
         ext_class_alloc< T >::value ||
         ext_mem_alloc< T >::value >{};

template < class T >
using ext_alloc_t =
    typename std::enable_if< ext_alloc< T >::value >::type;

} //end namespace Buffer
#endif /* END RAFT_ALLOCATE_BUFFER_INLINE_TRAITS_TCC */
