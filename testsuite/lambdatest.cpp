#include <cstddef>
#include <raft>
#include <raftio>

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <random>
#include "defs.hpp"

int
main()
{
    using type_t = std::int32_t;
    using print  = raft::print< type_t, '\n' >;
    using rnd    = raft::lambdak< type_t >;
    rnd lk( /** input ports      **/ 0,
            /** output ports     **/ 1,
            /** the compute func **/
        [&]( raft::StreamingData &dataIn,
             raft::StreamingData &bufOut )
        {
            UNUSED( dataIn );
            static std::default_random_engine generator;
            static std::uniform_int_distribution< type_t > distribution(1,10);
            static auto rand_func = std::bind( distribution, generator );
            static std::size_t gen_count( 0 );
            if( gen_count++ < 10000 )
            {
               bufOut[ "0"_port ].push( rand_func() );
               return( raft::kstatus::proceed );
            }
            return( raft::kstatus::stop );
        } );

    print p;
    raft::DAG dag;

    dag += lk >> p;
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
