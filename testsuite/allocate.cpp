#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include "generate.tcc"
#include "pipeline.tcc"

template< class A, class B, class C >
using Sum = raft::test::sum< A, B, C >;

int
main( int argc, char **argv )
{
    int count( 1000 );
    if( argc == 2 )
    {
       count = atoi( argv[ 1 ] );
    }

    //int64
    {
        using type_t = std::int64_t;
        using gen = raft::test::generate< type_t >;
        using sum = Sum< type_t, type_t, type_t >;
        using p_out = raft::print< type_t, '\n' >;
        gen a( count ), b( count );
        sum s;
        p_out print;

        raft::DAG dag;
#ifdef STRING_NAMES
        dag += a >> s[ "input_a" ];
        dag += b >> s[ "input_b" ];
#else
        dag += a >> s[ "input_a"_port ];
        dag += b >> s[ "input_b"_port ];
#endif
        dag += s >> print;
        dag.exe< raft::RuntimeFIFO >();
    }

    //bool
    {
        using type_t = bool;
        using gen = raft::test::generate< type_t >;
        using sum = Sum< type_t, type_t, type_t >;
        using p_out = raft::print< type_t, '\n' >;
        gen a( count ), b( count );
        sum s;
        p_out print;

        raft::DAG dag;
#ifdef STRING_NAMES
        dag += a >> s[ "input_a" ];
        dag += b >> s[ "input_b" ];
#else
        dag += a >> s[ "input_a"_port ];
        dag += b >> s[ "input_b"_port ];
#endif
        dag += s >> print;
        dag.exe< raft::RuntimeFIFO >();
    }

    //std::string
    {
        using type_t = std::string;
        using gen = raft::test::generate< type_t >;
        using sum = Sum< type_t, type_t, type_t >;
        using p_out = raft::print< type_t, '\n' >;
        gen a( count ), b( count );
        sum s;
        p_out print;

        raft::DAG dag;
#ifdef STRING_NAMES
        dag += a >> s[ "input_a" ];
        dag += b >> s[ "input_b" ];
#else
        dag += a >> s[ "input_a"_port ];
        dag += b >> s[ "input_b"_port ];
#endif
        dag += s >> print;
        dag.exe< raft::RuntimeFIFO >();
    }

    return( EXIT_SUCCESS );
}
