#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include <tuple>
#include "generate.tcc"
#include "defs.hpp"
#include "pipeline.tcc"


int
main( int argc, char **argv )
{
    int count( 1000 );
    if( argc == 2 )
    {
        count = atoi( argv[ 1 ] );
    }
    using send_t = std::int64_t;
    using wrong_t = float;
    using gen = raft::test::generate< send_t >;
    using sum = raft::test::sum< send_t,
                                 wrong_t,
                                 send_t >;
    using p_out = raft::print< send_t, '\n' >;
    raft::DAG dag;
    gen g1( count ), g2( count );
    sum s;
    p_out p;

    dag += g1 >> s[ "input_a"_port ];

    try
    {
        dag += g2 >> s[ "input_b"_port ];
    }
    catch( raft::PortTypeMismatchException &ex )
    {
        UNUSED( ex );
        std::cout << ex.what() << "\n";
        //yippy, we threw the right exception
        return( EXIT_SUCCESS );
    }
    dag += s >> p;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_FAILURE );
}
