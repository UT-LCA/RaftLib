/**
 * disconnectedGraph - check to ensure that the 
 * proper exception is thrown if the graph is not 
 * fully connected when exe is called.
 */

#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include "generate.tcc"
#include "pipeline.tcc"


template< class A, class B, class C >
class Sum : public raft::test::sum< A, B, C >
{
public:
    Sum() : raft::test::sum< A, B, C >()
    {
        (this)->template add_input< A >( "fail" );
    }

};


int
main( int argc, char **argv )
{
    bool success(false);
    int count( 1000 );
    if( argc == 2 )
    {
        count = atoi( argv[ 1 ] );
    }
    using type_t = std::int64_t;
    using gen = raft::test::generate< type_t >;
    using sum = Sum< type_t, type_t, type_t >;
    using p_out = raft::print< type_t, '\n' >;
    gen a( count ), b( count );
    sum s;
    p_out print;

    raft::DAG dag;
    dag += a >> s[ "input_a" ];
    dag += b >> s[ "input_b" ];
    dag += s >> print;
    try
    {
        dag.exe< raft::RuntimeFIFO >();
    }
    catch( raft::PortUnconnectedException &ex )
    {
        std::cerr << ex.what() << "\n";
        success = true;
    }

    return( success ? EXIT_SUCCESS : EXIT_FAILURE );
}
