/**
 * autoreleaseiterator.tcc - 
 * @author: Jonathan Beard
 * @version: Thu Jul  2 12:26:23 2020
 * 
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
#ifndef AUTORELEASEITERATOR_TCC
#define AUTORELEASEITERATOR_TCC  1

#include <iterator>
#include <
//TODO, add enable_if to sensure AUTORELEASE object inherits from 
//autorelease


/**
 * PLAN: 
 * constructor takes in: FIFO object, queue, 
 * n_items, basically most of the things that
 * the autorelease object has, we can't use the
 * [] overload b/c of the exception, but...
 * we can use the rest of the info directly. 
 * 
 * NOTE: we'll also need to pass through 
 * a way to keep the queue tied up while
 * the iterators still exist, i.e., we can't
 * recycle till we destruct these, meaning
 * they extend the ref-count of the parent
 * autorelease object.
 * 
 * Iterator ret val will be a std::pair, 
 * the first value must be the q value
 * the second is the signal so it'll work
 * with things like std::sort for in-place
 * sort. We'll also need to figure out how
 * to return a std::pair ref :). 
 */
#include <cstdlib>
#include <algorithm>
#include <functional> 

template < class T > class autorelease_iterator : 
    public std::iterator< std::forward_iterator_tag, std::pair< T&, Buffer:Signal >
{

using auto_ret_pair_t = 
    std::pair< std::reference_wrapper< T >, std::reference_wrapper< Buffer::Signal >
   


public:
    constepxr
    explicit autorelease_iterator(   T * const queue,
                                     Buffer::Signal * const signal,
                                     const std::size_t n_items,
                                     const std::size_t queue_size,
                                     const std::size_t crp
                                     )  :   queue( queue ),
                                            signal( signal ),
                                            n_items( n_items ),
                                            queue_size( queue_size ),
                                            crp( crp ),
                                            is_end( false )
    {
        //nothing here
    }

    constexpr
    explicit autorelease_iterator( const T * const queue,
                                   const bool is_end )  :   queue( queue ),
                                                            is_end( is_end ){}
   

    autorelease_iterator& operator++()
    {
        current_location = (index + crp) % queue_size;
    }
    
    bool operator   ==  ( const autorelease_iterator& rhs ) const
    {
        
    }

    bool operator   !=  ( const autorelease_iterator& rhs ) const
    {

    }
    
    operator*() const
    {

    }
    
    const std::string& name() const;

private:
    std::size_t              current_location;

    T    * const             queue;
    Buffer::Signal * const   signal;
    const std::size_t        n_items;
    const std::size_t        queue_size;
    const std::size_t        crp;
    const bool               is_end;
};

#endif /* END AUTORELEASEITERATOR_TCC */
