#include <raft>
#include <raftio>
#include <raftalgorithm>
#include <vector>
#include <cstdlib>
#include <iostream>

int
main()
{
    using chunk  = raft::filechunk< 512 >;
    using fr     = raft::filereader< chunk, false >;
    using search = raft::search< chunk, raft::stdlib >;
    std::vector< raft::match_t > matches;


    const std::string term( "Alice" );
    raft::DAG dag;
    fr   read( "./alice.txt" /** ex file **/, 
               (fr::offset_type) term.length(),
               1 );

    search find( term );
    auto we( raft::write_each< raft::match_t >( 
            std::back_inserter( matches ) ) );  
    dag += read >> find * 3 >> we;
    /** dag.exe() is an implicit barrier for completion of execution **/
    dag.exe< raft::RuntimeFIFO >();
    if( matches.size() != 174 /** count from grep **/ )
    {
        std::cout << matches.size() << std::endl;
        return( EXIT_FAILURE );
    }
    std::cout << matches.size() << "\n"; 
    return( EXIT_SUCCESS );
}
