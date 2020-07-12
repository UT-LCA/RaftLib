#include "roundrobin.hpp"

roundrobin::roundrobin( Port &port ) : splitmethod( port )
{
}


FIFO& 
roundrobin::select_fifo( bool &cont )
{
    cont = true;
    const auto tmp_begin = _port.begin();
    const auto tmp_end   = _port.end();

    if( begin != tmp_begin  || end != tmp_end )
    {
        std::cout << "here\n";
        //just reset all positions
        begin   = tmp_begin;
        current = tmp_begin;
        end     = tmp_end;
    }

    auto &output( (*current) );
    if( ++current == end )
    {
        cont = false;
        current = begin;
    }
    return( output );
}
