#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cassert>

#include "allocate/buffer/inline_traits.tcc"

template < std::size_t N > struct varlen
{
   char pad[ N ];
};

int
main()
{
   assert( Buffer::fits_in_cache_line< varlen< L1D_CACHE_LINE_SIZE > >::value );
   assert( 
      Buffer::fits_in_cache_line< varlen< L1D_CACHE_LINE_SIZE + 100 > >::value == false );
   exit( EXIT_SUCCESS );
}
