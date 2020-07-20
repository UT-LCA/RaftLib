/**
 * roundrobin.hpp - 
 * @author: Jonathan Beard
 * @version: Tue Oct 28 13:05:38 2014
 * 
 * Copyright 2014 Jonathan Beard
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
#ifndef RAFTROUNDROBIN_HPP
#define RAFTROUNDROBIN_HPP  1

#include "fifo.hpp"
#include "port.hpp"
#include "splitmethod.hpp"

class roundrobin : public splitmethod 
{
public:
    /**
     * default constructor for roundrobin scheduling
     * policy.
     * @param Port &ports - either input or output
     */
    roundrobin( Port &ports );
    /**
     * default destructor, doesn't 
     * do too much in this case. 
     */
    virtual ~roundrobin() = default;

protected:
    /**
     * select_fifo - this function should return
     * a valid FIFO object based on the implemented 
     * policy. In order to use this effectively, this
     * splitmethod object must be constructed with the
     * correct port container, e.g., "input" or "outptut"
     * @return FIFO - valid port object chosen by implemented
     * selection policy. 
     */
    virtual FIFO* select_input_fifo ( const std::size_t nitems ) = override;

    virtual FIFO* select_output_fifo( const std::size_t nitems ) = override;
};
#endif /* END RAFTROUNDROBIN_HPP */
