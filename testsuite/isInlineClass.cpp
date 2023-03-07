#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstddef>
#include "allocate/buffer/inline_traits.tcc"
#include "defs.hpp"
#include "foodef.tcc"


int
main( int argc, char **argv )
{
   UNUSED( argc );
   if( Buffer::fits_in_cache_line< foo< 4 > >::value != true )
   {
       std::cerr << "test (" << argv[ 0 ] << ") failed\n";
       exit( EXIT_FAILURE );
   }
   /** should be inline class allocate **/
   if( Buffer::inline_class_alloc< foo< 4 > >::value != true )
   {
       std::cerr << "test (" << argv[ 0 ] << ") failed\n";
       exit( EXIT_FAILURE );
   }
   
   /** should be fundamental type, and be false **/
   if( Buffer::inline_class_alloc< int >::value != false )
   {
       std::cerr << "test (" << argv[ 0 ] << ") failed\n";
       exit( EXIT_FAILURE );
   }
   /** should be too big, ret false **/
   if( Buffer::inline_class_alloc< foo< 128 > >::value != false )
   {
       std::cerr << "test (" << argv[ 0 ] << ") failed\n";
       exit( EXIT_FAILURE );
   }

   /** should be false, not a class **/
   if( Buffer::inline_class_alloc< int[ 2 ] >::value != false )
   {
       std::cerr << "test (" << argv[ 0 ] << ") failed\n";
       exit( EXIT_FAILURE );
   }
   exit( EXIT_SUCCESS );
}
