#include <cassert>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <raft>
#include <raftio>
#include <tuple>
#include "generate.tcc"
#include "defs.hpp"


template< typename A, typename B, typename C > class Sum : public raft::Kernel
{
public:
    Sum() : raft::Kernel()
    {
        add_input< A >( "input_a" );
        add_input< B >( "input_b" );
        add_output< C >( "sum" );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return task->pop( "input_a", dryrun ) &&
               task->pop( "input_b", dryrun );
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "sum", dryrun );
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        A a;
        B b;
        dataIn[ "input_a" ].pop< A >( a, task );
        dataIn[ "input_b" ].pop< B >( b, task );
        C c( static_cast< C >( a + b ) );
        bufOut[ "sum" ].push< C >( c, task );
        //if( sig_b == raft::eof )
        //{
        //   return( raft::stop );
        //}
        return( raft::kstatus::proceed );
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
    using send_t = std::int64_t;
    using wrong_t = float;
    using gen   = raft::test::generate< send_t >;
    using sum = Sum< send_t,
                     wrong_t,
                     send_t >;
    using p_out = raft::print< send_t, '\n' >;
    raft::DAG dag;
    gen g1( count ), g2( count );
    sum s;
    p_out p;

    dag += g1 >> s[ "input_a" ];

    try
    {
        dag += g2 >> s[ "input_b" ];
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
