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


template< typename A, typename B, typename C > class Sum : public raft::Kernel
{
public:
    Sum() : raft::Kernel()
    {
        add_input< A >( "input_a", "input_b", "fail" );
        add_output< C  >( "sum" );
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
                                            raft::StreamingData &bufOut )
    {
        A &a( dataIn[ "input_a" ].get< A >() );
        B &b( dataIn[ "input_b" ].get< B >() );
        bufOut[ "sum" ].get< C >() = C( a + b );
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
