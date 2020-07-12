#include "roundrobin.hpp"

roundrobin::roundrobin( Port &port ) : splitmethod( port )
{
}


FIFO& 
roundrobin::select_fifo(  )
{
    auto &output( (*current) );
    if( ++current == end )
    {
        current = begin;
    }
    return( output );
}
