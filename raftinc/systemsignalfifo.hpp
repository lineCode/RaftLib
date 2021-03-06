/**
 * systemsignalfifo.hpp - 
 * @author: Jonathan Beard
 * @version: Fri Jan  2 19:49:31 2015
 * 
 * Copyright 2015 Jonathan Beard
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
#ifndef _SYSTEMSIGNALFIFO_HPP_
#define _SYSTEMSIGNALFIFO_HPP_  1

#include "signalvars.hpp"

class Schedule;

class SystemSignalFIFO
{
protected:

   /**  
    * send_system_signal - send a signal that isn't aligned with the 
    * data.
    * @signal - const raft::signal
    */
   virtual void send_system_signal( const raft::signal &&signal ) = 0;

   /**
    * get_system_signal - returns the current 
    * signal, pops it from the FIFO afterwards.
    * If no signal exists then raft::none is returned.
    *
    * @return raft::signal
    */
   virtual raft::signal get_system_signal() = 0;

};
#endif /* END _SYSTEMSIGNALFIFO_HPP_ */
