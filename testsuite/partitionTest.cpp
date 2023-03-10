#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include "generate.tcc"

int
main( int argc, char **argv )
{
    int count( 1000 );
    if( argc == 2 )
    {
        count = atoi( argv[ 1 ] );
    }
                                                               
    raft::test::generate< std::uint32_t > rndgen( count );
    raft::print< std::uint32_t, '\n' > p;

    using sub = raft::lambdak< std::uint32_t >;
    auto l_sub( []( raft::StreamingData &dataIn,
                    raft::StreamingData &bufOut,
                    raft::Task *task ) -> raft::kstatus::value_t
    {
        std::uint32_t a;
        dataIn[ "0" ].pop( a, task );
        bufOut[ "0" ].push( a - 10, task );
        return( raft::kstatus::proceed );
    } );

    raft::DAG dag;
    /** make one sub kernel, this one will live on the stack **/
    sub s( 1, 1, l_sub );
    raft::Kpair *kpair = &( rndgen >> s );
    for( int i( 0 ); i < 
#ifdef USEQTHREADS
    1000
#else
    5
#endif
    ; i++ )
    {
        kpair = &( ( *kpair ) >> raft::kernel_maker< sub >( 1, 1, l_sub ) );
    }
    dag += *kpair >> p;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
