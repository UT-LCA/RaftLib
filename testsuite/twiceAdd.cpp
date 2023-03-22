/**
 * test case to detect the twice add behavior
 * mentioned in github Issue #29, original
 * example was: m += a >> b; m += a >> c;
 */

#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include "generate.tcc"
#include "pipeline.tcc"

template< typename T >
using sum = raft::test::sum< T, T, T >;

int
main( int argc, char **argv )
{
    int count( 1000 );
    if( argc == 2 )
    {
        count = atoi( argv[ 1 ] );
    }

    using send_t = std::int64_t;
    using gen = raft::test::generate< send_t >;

    using add = sum< send_t >;
    using p_out = raft::print< send_t, '\n' >;

    gen a( count ), b( count );
    add s;
    p_out print;

    raft::DAG dag;
    /**
     * adding twice, should throw an exeption
     */
    try
    {
        dag += a >> s[ "input_a"_port ];
        dag += a >> s[ "input_b"_port ];
        dag += b >> s[ "input_b"_port ];
        dag += s >> print;
        dag.exe< raft::RuntimeFIFO >();
    }
    catch( raft::PortDoubleInitializeException &ex )
    {
        std::cout << ex.what() << "\n";
        exit( EXIT_SUCCESS );
    }
    return( EXIT_FAILURE );
}
