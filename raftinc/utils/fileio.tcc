/**
 * fileio.tcc -
 * @author: Jonathan Beard, Qinzhe Wu
 * @version: Tue Mar 07 12:16:00 2023
 *
 * Copyright 2023 The Regents of the University of Texas
 * Copyright 2020 Jonathan Beard
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
#ifndef RAFT_UTILS_FILEIO_TCC
#define RAFT_UTILS_FILEIO_TCC  1
#include <cstdlib>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <sys/stat.h>
#include <iostream>
#include <cmath>
#include <raft>
#include <type_traits>

#include "raftinc/utils/filechunk.tcc"
#include "raftinc/utils/chunkiterator.tcc"


namespace raft{

enum readertype : std::int8_t { chunk, fasta };

template < class chunktype = filechunk< 65536 >,
           bool copy = false >
class filereader : public raft::Kernel
{
public:
    filereader( const std::string inputfile,
                const long chunk_offset = 0,
                const std::int64_t repetitions = 1
                ) :
        Kernel(), chunk_offset( chunk_offset )
    {
#if STRING_NAMES
        add_output< chunktype >( "0" );
#else
        add_output< chunktype >( "0"_port );
#endif

        /** stat file **/
        struct stat st;
        std::memset( &st, 0, sizeof( struct stat ) );
        if( stat( inputfile.c_str(), &st ) != 0 )
        {
            perror( "Failed to stat input file, exiting!" );
            //TODO, figure out global shutdown procedure
            exit( EXIT_FAILURE );
        }

        /** initialize file **/
#if (  defined __WIN64 ) || ( defined __WIN32 ) || ( defined _WINDOWS )
        if( fopen_s( &fp, inputfile.c_str(), "r" ) != 0 )
        {
             std::cerr << "failed to open input file: (" + inputfile +
                 "), exiting\n";
             exit( EXIT_FAILURE );
        }
#else
        fp = std::fopen( inputfile.c_str() , "r" );
        if( fp == nullptr )
        {
             perror( "Failed to open input file, exiting!" );
             exit( EXIT_FAILURE );
        }
#endif

        /** get length in bytes **/
        length = st.st_size;
        if( chunk_offset != 0 )
        {
            iterations = std::ceil(
                    (float) length /
                    (float) ( ( chunktype::getChunkSize() - 1) -
                              ( chunk_offset - 1) ) );
        }
        else
        {
            iterations = std::ceil(
                    (float) length /
                    (float) ( chunktype::getChunkSize() - 1 ) );
        }
        nchunks = iterations;
        iterations *= repetitions;
    }

    virtual bool pop( raft::Task *task, bool dryrun )
    {
        return true;
    }

    virtual bool allocate( raft::Task *task, bool dryrun )
    {
#if STRING_NAMES
        return task->allocate( "0", dryrun );
#else
        return task->allocate( "0"_port, dryrun );
#endif
    }

    virtual raft::kstatus::value_t compute( StreamingData &dataIn,
                                            StreamingData &bufOut )
    {
        auto &chunk( bufOut.template allocate< chunktype >() );
        if( init )
        {
            fseek( fp, - chunk_offset , SEEK_CUR );
        }
        else
        {
            init = true;
        }
        chunk.start_position = ftell( fp );
        chunk.index = chunk_index;
        chunk_index++;
        const auto chunksize( chunktype::getChunkSize() );
        const auto num_read(
           fread( chunk.buffer, sizeof( char ), chunksize - 1 , fp ) );
        chunk.buffer[ num_read ] = '\0';
        chunk.length = num_read;
        static_assert( std::is_signed< decltype( iterations ) >::value,
                       "iterations must be a signed type" );
        bufOut.send();
        if( --iterations <= 0 )
        {
            return( kstatus::stop );
        }
        if ( 0 == ( iterations % nchunks ) ) {
            fseek( fp, 0, SEEK_SET );
            init = false;
        }
        return( kstatus::proceed );
    }
    using offset_type = long;
private:
    /** opened in the constructor **/
    FILE *fp = nullptr;
    std::streamsize length = 0;
    std::int64_t iterations = 0;
    bool init = false;
    std::uint64_t chunk_index = 0;
    offset_type chunk_offset;
    std::int64_t nchunks = 0;
};

} /* end namespace raft */
#endif /* END RAFT_UTILS_FILEIO_TCC */