/**
 * Example from Issue #43, seg fault when no port is 
 * available to connect to. Should throw an exception
 */

#include <raft>
#include <raftio>


struct big_t
{
    int i;
    std::uintptr_t start;
#ifdef ZEROCPY
    char padding[ 100 ];
#else
    char padding[ 32 ];
#endif
};



/**
 * Producer: sends down the stream numbers from 1 to 10
 */
class A : public raft::Kernel
{
private:
    int i   = 0;

public:
    A() : raft::Kernel()
    {
        add_output< big_t >( "out" );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return false;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return task->allocate( "out", dryrun );
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        i++;

        if ( i <= 10 ) 
        {
            auto &c( bufOut[ "out" ].allocate< big_t >( task ) );
            c.i = i;
            c.start = reinterpret_cast< std::uintptr_t >( &(c.i) );
            bufOut[ "out" ].send( task );
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
        //add_input< big_t >( "in" );
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return task->pop( "in", dryrun );
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        auto &a( dataIn[ "in" ].peek< big_t >( task ) );
        std::cout << std::dec << a.i << " - " << std::hex << a.start << " - " << std::hex <<  
            reinterpret_cast< std::uintptr_t >( &a.i ) << "\n";
        dataIn[ "in" ].recycle( task );
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
