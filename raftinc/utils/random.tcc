/**
 * random.tcc -
 * @author: Jonathan Beard
 * @version: Mon Jan  4 15:17:03 2016
 *
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
#ifndef RAFT_UTILS_RANDOM_TCC
#define RAFT_UTILS_RANDOM_TCC  1
/** use cpp rnd generator **/
#include <random>
#include <raft>
#include <cstddef>
#include <chrono>

namespace raft
{
template < class GENERATOR,
           template < class > class DIST,
           class TYPE >
class random_variate : public parallel_k
{
public:
    template <class ... Args >
    random_variate( const std::size_t N,
                    Args&&... params ) : parallel_k(),
        gen(
         static_cast< typename GENERATOR::result_type >(
            std::chrono::system_clock::now().time_since_epoch().count() ) ),
                                         dist( std::forward< Args >( params )... ),
                                         N( N )
    {
#ifdef STATICPORT
        for( auto i( 0 ); i < STATICPORT; i++ )
        {
#endif
        std::size_t idx = inc_output< TYPE >();
        auto outname( std::to_string( idx ) );
#if STRING_NAMES
        out_names.push_back( outname );
#else
        out_names.push_back( raft::port_name_t( outname.c_str(), outname.size() ) );
#endif
#ifdef STATICPORT
        }
#endif
    }

    random_variate( const random_variate &other ) : parallel_k(),
       gen( static_cast< typename GENERATOR::result_type >(
            std::chrono::system_clock::now().time_since_epoch().count() ) ),
                                                    dist( other.dist ),
                                                    N( other.N )
    {
#ifdef STATICPORT
        for( auto i( 0 ); i < STATICPORT; i++ )
        {
#endif
        inc_output< TYPE >();
#ifdef STATICPORT
        }
#endif
    }

    virtual ~random_variate() = default;

    /** enable cloning **/
    //CLONE();

    virtual raft::kstatus::value_t compute( StreamingData &dataIn,
                                            StreamingData &bufOut )
    {
        for( auto &name : out_names )
        {
            bufOut[ name ].push( dist( gen ) );
            if( ++count_of_sent >= N )
            {
                return( raft::kstatus::stop );
            }
        }
        return( raft::kstatus::proceed );
    }

    virtual bool pop( Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( Task *task, bool dryrun )
    {
        return true;
    }

private:
    GENERATOR       gen;
    DIST< TYPE >    dist;
    std::size_t     count_of_sent = 0;
    const std::size_t N;
    std::vector< port_name_t > out_names;
};


}

#endif /* END RAFT_UTILS_RANDOM_TCC */
