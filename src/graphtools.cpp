/**
 * graphtools.cpp -
 * @author: Jonathan Beard
 * @version: Sat Sep 20 13:15:09 2014
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
#include <algorithm>
#include <set>
#include <map>
#include <queue>
#include <utility>
#include <string>
#include <sstream>
#include <mutex>

#include "portmap_t.hpp"
#include "common.hpp"
#include "portmap_t.hpp"
#include "portexception.hpp"
#include "graphtools.hpp"
#include "port_info.hpp"
#include "kernel.hpp"
#include "tempmap.hpp"

void
GraphTools::BFS( std::set< raft::kernel* > &source_kernels,
                 edge_func func,
                 void      *data,
                 bool      connected_error )
{
   std::set< raft::kernel* > visited_set;
   std::queue< raft::kernel* >     queue;
   std::for_each( source_kernels.begin(),
                  source_kernels.end(),
                  [&]( raft::kernel *k )
                  {
                     queue.push( k );
                     visited_set.insert( k );
                  } );
   GraphTools::__BFS( queue, visited_set, func, data, connected_error );
}

void
GraphTools::BFS( std::vector< raft::kernel* > &source_kernels,
                 edge_func func,
                 void      *data,
                 bool      connected_error )
{
   std::set< raft::kernel* >       visited_set;
   std::queue< raft::kernel* >     queue;
   std::for_each( source_kernels.begin(),
                  source_kernels.end(),
                  [&]( raft::kernel *k )
                  {
                     queue.push( k );
                     visited_set.insert( k );
                  } );
   GraphTools::__BFS( queue, visited_set, func, data, connected_error );
}


void
GraphTools::BFS( std::set< raft::kernel* > &source_kernels,
                 vertex_func                 func,
                 void                        *data )
{
   std::queue< raft::kernel* >   queue;
   std::set< raft::kernel* >     visited_set;
   std::for_each( source_kernels.begin(),
                  source_kernels.end(),
                  [&]( raft::kernel * k )
                  {
                     queue.push( k );
                     visited_set.insert( k );
                  });

   GraphTools::__BFS( queue, visited_set, func, data );
   return;
}

raft::temp_map*
GraphTools::duplicateFromVertexToSource( raft::kernel * const start )
{
    assert( start != nullptr );
    struct Data
    {
        Data() : temp_map( new raft::temp_map() )
        {
        }
        /**
         * NOTE: this map contains a reference from the pointers
         * cast to uintptr_t (set as the key) that are the original 
         * (to be duplicated kernels) to the new one which is the 
         * value. 
         */
        std::map< std::uintptr_t, raft::kernel * > kernel_map;
        /** 
         * for graphs with a feedback loop, back-edges, we could
         * in fact have an edge that appears in the BFS whose
         * other terminal end has yet to be reached.
         */
        std::map< std::uintptr_t, PortInfo* >       unmatched;
        raft::temp_map                             *temp_map     = nullptr;
    }   d;
    /** make structure to hold previously cloned kernel **/
    vertex_func f( []( raft::kernel *current,
                       void         *data ) -> void
    {
        Data *d( reinterpret_cast< Data* >( data ) );
        auto *cloned_kernel( current->clone() );
        if( cloned_kernel == nullptr )
        {
            //TODO throw an exception
            std::cerr << 
                "attempting to clone a kernel that wasn't meant to be cloned()\n";
            exit( EXIT_FAILURE );
        }
        /** first kernel **/
        if( d->kernel_map.size() == 0 )
        {
            const auto d_ret_val( d->kernel_map.insert( std::make_pair( 
                reinterpret_cast< std::uintptr_t >( current ),
                cloned_kernel ) ) );
#if NDEBUG
            UNUSED( d_ret_val );
#else
            assert( d_ret_val.second == true );
#endif
            return;
        }
        else
        {
            
            const auto d_ret_val( d->kernel_map.insert( std::make_pair( 
                reinterpret_cast< std::uintptr_t >( current ),
                cloned_kernel ) ) );
#if NDEBUG
            UNUSED( d_ret_val );
#else
            assert( d_ret_val.second == true );
#endif
            /** 
             * loop over output ports of "current" kernel and hook the
             * corresponding ports of the cloned kernel to the corresponding
             * ports on the cloned output kernels in the map (inside data 
             * struct).
             */
            while( ! current->output.portmap.mutex_map.try_lock() )
            {
               std::this_thread::yield();
            }
            //we have lock, continue
            /** 2) get map **/
            auto &map_of_ports( current->output.portmap.map );
            for( auto &port : map_of_ports )
            {   
                /** port.first is the name **/
                auto &port_info( port.second );
                /**
                 * we want to link clone.out to the 
                 * output kernel clone saved in the 
                 * map that corresponds to the memory
                 * location then link it to the new 
                 * kernels port.
                 */
                const auto dst_kern_hash( 
                    reinterpret_cast< std::uintptr_t >( 
                        port_info.other_kernel
                    )
                );
                /** 
                 * look up this destination kernel in our
                 * clone map.
                 */
                auto found( d->kernel_map.find( dst_kern_hash ) );
                if( found == d->kernel_map.end() )
                {
                    /** destination not yet reached, likely back edge **/
                    d->unmatched.insert( 
                        std::make_pair( 
                            reinterpret_cast< std::uintptr_t >( current ), &port_info
                        )
                    );
                }
                else
                {
                    /** 
                     * we found a match, lets link up the new 
                     * kernel.
                     */
                    d->temp_map->link(
                        cloned_kernel       /** which kernel **/,
                        port_info.my_name   /** port name    **/,
                        found->second        /** cloned destination **/,
                        port_info.other_name /** destination **/,
                        ( port_info.out_of_order ? raft::order::out : raft::order::in )
                    );

                }
                /** go through unmatched and see if we can match some up **/
            }
            /** UNLOCK **/
            /** go through unmatched and see if we can match some up **/
            //TODO

            /** if any unmatched kernels...somethings wrong throw exception **/


            current->output.portmap.mutex_map.unlock();
        }
        
        /** else lets get this going **/
        /** 
         * NOTE: need condition if sub_map is empty
         * this will be a momentary condition since
         * the source to this duplication is required
         * to have only a single exit point
         */
    } );
    assert( d.temp_map != nullptr );
    return( d.temp_map );
}

raft::temp_map*
GraphTools::duplicateFromVertexToSink( raft::kernel * const start )
{
    UNUSED( start );
    /** 
     * add structure to hold newly cloned head, since
     * function should return the clone of "start"
     */
    //auto 
    ///** make structure to hold previously cloned kernel **/
    //vertex_func f( []( raft::kernel *k,
    //                   void         *data )
    //{
    //    /** call clone **/
    //    
    //};
    return( nullptr );
}

void
GraphTools::__BFS( std::queue< raft::kernel* > &queue,
                   std::set<   raft::kernel* > &visited_set,
                   edge_func                   func,
                   void                        *data,
                   bool                        connected_error )
{
   while( queue.size() > 0 )
   {
      auto *k( queue.front() );
      queue.pop();
      if( k == nullptr ) break;
      /** iterate over all out-edges **/
      /** 1) get lock **/
      while( ! k->output.portmap.mutex_map.try_lock() )
      {
         std::this_thread::yield();
      }
      //we have lock, continue
      /** 2) get map **/
      auto &map_of_ports( k->output.portmap.map );
      for( auto &port : map_of_ports )
      {
         PortInfo &source( port.second );
         /** get dst edge to call function on **/
         if( source.other_kernel != nullptr  )
         {
            PortInfo &dst(
               source.other_kernel->input.getPortInfoFor( source.other_name ) );
            func( source, dst, data );
         }
         else
         if( connected_error )
         {
            std::stringstream ss;
            ss << "Unconnected port detected at " <<
               common::printClassName( *k ) <<
                  "[ \"" <<
                  source.my_name << " \"], please fix and recompile.";
            k->output.portmap.mutex_map.unlock();
            throw PortException( ss.str() );
         }
         /** if the dst kernel hasn't been visited, visit it **/
         if( visited_set.find( source.other_kernel ) == visited_set.end() )
         {
            queue.push( source.other_kernel );
            visited_set.insert( source.other_kernel );
         }
      }
      k->output.portmap.mutex_map.unlock();
   }
   return;
}

void
GraphTools::__BFS( std::queue< raft::kernel* > &queue,
                   std::set< raft::kernel*   > &visited_set,
                   vertex_func                 func,
                   void                        *data )
{
   while( queue.size() > 0 )
   {
      auto *source( queue.front() );
      if( source == nullptr )
      {
         break;
      }
      queue.pop();
      /** iterate over all out-edges **/
      /** 1) get lock **/
      while( ! source->output.portmap.mutex_map.try_lock() )
      {
         std::this_thread::yield();
      }
      /** 2) get map **/
      auto &map_of_ports( source->output.portmap.map );
      /** 3) visit kernel **/
      func( source, data );
      /** 4) add children to queue **/
      for( auto &port : map_of_ports )
      {
         PortInfo &source( port.second );
         /** get dst edge to call function on **/
         if( source.other_kernel != nullptr  )
         {
            /** if the dst kernel hasn't been visited, visit it **/
            if( visited_set.find( source.other_kernel ) == visited_set.end() )
            {
               queue.push( source.other_kernel );
               visited_set.insert( source.other_kernel );
            }
         }
      }
      source->output.portmap.mutex_map.unlock();
   }
   return;
}

void
GraphTools::__DFS( std::stack< raft::kernel* > &stack,
                   std::set<   raft::kernel* > &visited_set,
                   edge_func                   func,
                   void                        *data )
{
   UNUSED( stack );
   UNUSED( visited_set );
   UNUSED( func );
   UNUSED( data );

   /** TODO, implement me **/
   assert( false );
}

void
GraphTools::__DFS( std::stack< raft::kernel* > &stack,
                   std::set<   raft::kernel* > &visited_set,
                   vertex_func                 func,
                   void                        *data )
{
   UNUSED( stack );
   UNUSED( visited_set );
   UNUSED( func );
   UNUSED( data );

   /** TODO, implement me **/
   assert( false );
}
