#include "roundrobin.hpp"

roundrobin::roundrobin( Port &port ) : splitmethod( port )
{
}


FIFO* 
splitmethod::select_input_fifo ( const std::size_t nitems )
{
    const auto _tmp_begin = _port.begin();
    const auto _tmp_end   = _port.end();
    if( _tmp_begin != begin || _tmp_end != end )
    {
        begin   = current = _tmp_begin;
        end     =   _tmp_end;
    }
    for( ; current != end; ++current )
    {
        if( (*current).size() > nitems )
        {
            const auto output = current;
            ++current;
            return( &(*output) );
        }
    }
    if( current == end )
    {
        current = begin;
    }
    /** 
     * else, return nullptr. This function is a bit less than 
     * ideal given at invocation the current iterator might 
     * equal the end iterator, so, we'll fix that eventually,
     * but before we do that I want to make sure there's not
     * a far better way to do this even if we have to change 
     * the internal interface a bit. 
     */
    return( nullptr );
}

FIFO* 
splitmethod::select_output_fifo( const std::size_t nitems )
{
    return( nullptr );
}
