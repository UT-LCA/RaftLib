/**
 * Example from Issue #43, seg fault when no port is 
 * available to connect to. Should throw an exception
 */

#include <raft>
#include <raftio>
#include "pipeline.tcc"


using obj_t = int;

/**
 * Producer: sends down the stream numbers from 1 to 10
 */
class A : public raft::test::start< obj_t >
{
private:
    int i   = 0;

public:
    A() : raft::test::start< obj_t >()
    {
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        i++;

        if ( i <= 10 ) 
        {
            auto &c( bufOut[ "out"_port ].allocate< obj_t >() );
            c = i;
            bufOut[ "out"_port ].send();
        }
        else
        {   
            return ( raft::kstatus::stop );
        }

        return ( raft::kstatus::proceed );
    };
};

/**
 * Consumer: takes the number from input and dumps it to the console
 */
class C : public raft::Kernel
{
public:
    C() : raft::Kernel()
    {
        //add_input< obj_t >( "in" );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return task->pop( "in"_port, dryrun );
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut )
    {
        auto &a( dataIn[ "in"_port ].peek< obj_t >() );
        std::cout << a << "\n";
        dataIn[ "in"_port ].recycle();
        return ( raft::kstatus::proceed );
    }
};

int main()
{
    A a;
    C c;
    raft::DAG dag;
    try
    {
        dag += a >> c;
    }
    catch( raft::PortNotFoundException &ex )
    {
        std::cerr << ex.what() << "\n";
        /** success for test case at least **/
        exit( EXIT_SUCCESS );
    }
    dag.exe< raft::RuntimeFIFO >();
    return( EXIT_SUCCESS );
}
