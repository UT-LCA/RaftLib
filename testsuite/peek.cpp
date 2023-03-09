#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#define PEEKTEST 1
#include <raft>
#include <raftio>
#include <raftmath>
#include "generate.tcc"
#include "pipeline.tcc"


template < typename T > class Sum : public raft::test::sum< T, T, T >
{
public:
   Sum() : raft::test::sum< T, T, T >()
   {
   }

   virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                           raft::StreamingData &bufOut,
                                           raft::Task *task )
   {
      bufOut[ "sum" ].push( raft::sum< T >( input[ "input_a" ],
                                            input[ "input_b" ] ) );
      return( raft::proceed );
   }

};


int
main( int argc, char **argv )
{
   int count( 1000 );
   if( argc == 2 )
   {
      count = atoi( argv[ 1 ] );
   }
   using type_t = std::int64_t;
   using gen = raft::test::generate< type_t >;
   using sum = Sum< type_t >;
   using print  = raft::print< type_t, '\n' >;

   gen a( count ), b( count );
   sum s;
   print p;
   raft::DAG dag;
   dag += a >> s[ "input_a" ];
   dag += b >> s[ "input_b" ];
   dag += s >> p;
   dag.exe< raft::RuntimeFIFO >();
   return( EXIT_SUCCESS );
}
