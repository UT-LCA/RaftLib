/**
 * pipeline.tcc - some kernel templates used by multiple pipeline style tests.
 *
 * @author: Qinzhe Wu
 * @version: Wed Mar 08 10:50:26 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2016 Jonathan Beard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef PIPELINE_TCC
#define PIPELINE_TCC  1

#include <raft>

namespace raft
{

namespace test
{

template< class obj_t >
class start : public raft::Kernel
{
public:
    start() : raft::Kernel()
    {
#ifdef STRING_NAMES
        add_output< obj_t >( "y" );
#else
        add_output< obj_t >( "y"_port );
#endif
    }

    virtual ~start() = default;

    // leave compute() been defined in the tests
    //virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
    //                                        raft::StreamingData &bufOut,
    //                                        raft::Task *task )

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->allocate( "y", dryrun );
#else
        return task->allocate( "y"_port, dryrun );
#endif
    }

};


template< class objin_t, class objout_t >
class middle : public raft::Kernel
{
public:
    middle() : raft::Kernel()
    {
#ifdef STRING_NAMES
        add_input< objin_t >( "x" );
        add_output< objout_t >( "y" );
#else
        add_input< objin_t >( "x"_port );
        add_output< objout_t >( "y"_port );
#endif
    }

    // leave compute() been defined in the tests
    //virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
    //                                        raft::StreamingData &bufOut,
    //                                        raft::Task *task )

    virtual bool pop( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->pop( "x", dryrun );
#else
        return task->pop( "x"_port, dryrun );
#endif
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->allocate( "y", dryrun );
#else
        return task->allocate( "y"_port, dryrun );
#endif
    }
};


template< class obj_t >
class last : public raft::Kernel
{
public:
    last() : raft::Kernel()
    {
#ifdef STRING_NAMES
        add_input< obj_t >( "x" );
#else
        add_input< obj_t >( "x"_port );
#endif
    }

    virtual ~last() = default;

    // leave compute() been defined in the tests
    //virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
    //                                        raft::StreamingData &bufOut,
    //                                        raft::Task *task )

    virtual bool pop( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->pop( "x", dryrun );
#else
        return task->pop( "x"_port, dryrun );
#endif
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
        return true;
    }
};

template< class A, class B, class C >
class sum : public raft::Kernel
{
public:
    sum() : raft::Kernel()
    {
#ifdef STRING_NAMES
        add_input< A >( "input_a" );
        add_input< B >( "input_b" );
        add_output< C >( "sum" );
#else
        add_input< A >( "input_a"_port );
        add_input< B >( "input_b"_port );
        add_output< C >( "sum"_port );
#endif
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->pop( "input_a", dryrun ) &&
               task->pop( "input_b", dryrun );
#else
        return task->pop( "input_a"_port, dryrun ) &&
               task->pop( "input_b"_port, dryrun );
#endif
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#ifdef STRING_NAMES
        return task->allocate( "sum", dryrun );
#else
        return task->allocate( "sum"_port, dryrun );
#endif
    }

    virtual raft::kstatus::value_t compute( raft::StreamingData &dataIn,
                                            raft::StreamingData &bufOut,
                                            raft::Task *task )
    {
        A a;
        B b;
#ifdef STRING_NAMES
        dataIn[ "input_a" ].pop< A >( a, task );
        dataIn[ "input_b" ].pop< B >( b, task );
        C c( static_cast< C >( a + b ) );
        bufOut[ "sum" ].push< C >( c, task );
#else
        dataIn[ "input_a"_port ].pop< A >( a, task );
        dataIn[ "input_b"_port ].pop< B >( b, task );
        C c( static_cast< C >( a + b ) );
        std::cout << c << std::endl;
        bufOut[ "sum"_port ].push< C >( c, task );
#endif
        return( raft::kstatus::proceed );
    }

};

} //end namespace test

} //end namespace raft
#endif /* END GENERATE_TCC */
