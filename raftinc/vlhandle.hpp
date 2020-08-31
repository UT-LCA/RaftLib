#ifndef VLHANDLE_HPP 
#define VLHANDLE_HPP 1

#include <atomic>
#include <cstdint>

struct alignas( 64 ) VLHandle
{
#pragma pack( push, 1 )  
  //new cache line 
  int vl_fd  = 0;
  std::uint8_t   p1[ 64 - sizeof( int ) ];
  //new cache line 
  volatile bool  is_valid   = true;
  std::uint8_t   padding[ 64 - sizeof( bool ) ];
#pragma pack( pop )
  std::atomic<int> valid_count;
};

#endif /* VLHANDLE_HPP */
