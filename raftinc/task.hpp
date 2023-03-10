/**
 * task.hpp - the interface class of Task
 * @author: Qinzhe Wu
 * @version: Sun Feb 26 15:30:00 2023
 *
 * Copyright 2023 The Regents of the University of Texas
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
#ifndef RAFT_TASK_HPP
#define RAFT_TASK_HPP  1
#include <vector>

#include "exceptions.hpp"
#include "defs.hpp"
#include "rafttypes.hpp"

namespace raft
{

class DataRef;
class StreamingData;
class Allocate;
class Schedule;

enum TaskType
{
    POLLING_WORKER,
    ONE_SHOT
};

struct ALIGN( L1D_CACHE_LINE_SIZE ) Task
{
    Kernel *kernel;
    TaskType type;
    std::size_t id;
    bool finished = false;

    virtual kstatus::value_t exe() = 0;

    virtual void pop( const port_name_t &name, DataRef &item ) = 0;

    virtual DataRef peek( const port_name_t &name ) = 0;

    virtual void recycle( const port_name_t &name ) = 0;

    virtual void push( const port_name_t &name, DataRef &item ) = 0;

    virtual DataRef allocate( const port_name_t &name ) = 0;

    virtual void send( const port_name_t &name ) = 0;

    virtual bool pop( const port_name_t &portname, bool dryrun ) = 0;

    virtual bool allocate( const port_name_t &portname, bool dryrun ) = 0;

    virtual std::vector< port_name_t > &getNamesIn() = 0;

    virtual std::vector< port_name_t > &getNamesOut() = 0;

    virtual StreamingData &getDataIn() = 0;

    virtual StreamingData &getBufOut() = 0;
};

} /** end namespace raft */
#endif /* END RAFT_TASK_HPP */
