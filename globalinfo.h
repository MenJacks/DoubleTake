// -*- C++ -*-
/*
  Copyright (c) 2012, University of Massachusetts Amherst.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
 * @file   globalinfo.h
 * @brief  some global information about this system, by doing this, we can avoid multiple copies.
 *         Also, it is very important to utilize this to cooperate multiple threads since pthread_kill
 *         actually can not convey additional signal value information.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */
#ifndef _SYSINFO_H_
#define _SYSINFO_H_

#include <errno.h>

#include <stdlib.h>
#include "threadmap.h"
#include "threadstruct.h"
#include "xdefines.h"

extern "C" {
  enum SystemPhase {
    E_SYS_INIT, // Initialization phase
    E_SYS_NORMAL_EXECUTION, 
    E_SYS_EPOCH_END, // We are just before commit.  
    E_SYS_ROLLBACK, // We are trying to rollback the whole system.
    E_SYS_EPOCH_BEGIN, // We have to start a new epoch when no overflow. 
  };
  extern bool g_isRollback;
  extern bool g_hasRollbacked;
  extern int  g_numOfEnds;
  extern enum SystemPhase g_phase; 
  extern pthread_cond_t g_condCommitter;
  extern pthread_cond_t g_condWaiters;
  extern pthread_mutex_t g_mutex;
  extern pthread_mutex_t g_mutexSignalhandler;
  extern int g_waiters;  
  extern int g_waitersTotal;  

  inline void global_lock(void) {
    WRAP(pthread_mutex_lock)(&g_mutex);
  }

  inline void global_unlock(void) {
    WRAP(pthread_mutex_unlock)(&g_mutex);
  } 
  
  inline void global_lockInsideSignalhandler(void) {
    WRAP(pthread_mutex_lock)(&g_mutexSignalhandler);
  }

  inline void global_unlockInsideSignalhandler(void) {
    WRAP(pthread_mutex_unlock)(&g_mutexSignalhandler);
  }


  inline void global_initialize(void) {
    g_isRollback = false;
    g_hasRollbacked = false;
    g_phase = E_SYS_INIT;
    g_numOfEnds = 0;

   //fprintf(stderr, "global initializee............\n");
    WRAP(pthread_mutex_init)(&g_mutex, NULL);
    WRAP(pthread_mutex_init)(&g_mutexSignalhandler, NULL);
    WRAP(pthread_cond_init)(&g_condCommitter, NULL);
    WRAP(pthread_cond_init)(&g_condWaiters, NULL);
  }
 
  inline void global_setEpochEnd(void) {
    g_numOfEnds++;
    g_phase = E_SYS_EPOCH_END;
  }

  inline bool global_isInitPhase(void) {
    return g_phase == E_SYS_INIT;
  }

  inline bool global_isEpochEnd(void) {
    return g_phase == E_SYS_EPOCH_END;
  }

  inline bool global_isRollback(void) {
    //PRDBG("ISROLLLBACK g_phase %d E_SYS_ROLLBACK %d\n", g_phase, E_SYS_ROLLBACK);
    return g_phase == E_SYS_ROLLBACK;
  }

  inline bool global_isEpochBegin(void) {
    return g_phase == E_SYS_EPOCH_BEGIN;
  }

  inline void global_setRollback(void) {
    g_phase = E_SYS_ROLLBACK;
    g_hasRollbacked = true;
    //fprintf(stderr, "setting ROLLLBACK g_phase %d E_SYS_ROLLBACK %d\n", g_phase, E_SYS_ROLLBACK);
  }

  inline bool global_hasRollbacked(void) {
    return g_hasRollbacked;
  }

  inline void global_rollback(void) {
    global_setRollback();

    // Wakeup all other threads.
    WRAP(pthread_cond_broadcast)(&g_condWaiters);
    fprintf(stderr, "after setting ROLLLBACK g_phase %d E_SYS_ROLLBACK %d\n", g_phase, E_SYS_ROLLBACK);
  }

  inline void global_epochBegin(void) {
    global_lockInsideSignalhandler();

    g_phase = E_SYS_EPOCH_BEGIN;
    PRDBG("waken up all waiters\n");
    // Wakeup all other threads.
    WRAP(pthread_cond_broadcast)(&g_condWaiters);

    if(g_waiters != 0) {
      WRAP(pthread_cond_wait)(&g_condCommitter, &g_mutexSignalhandler);
    }
    global_unlockInsideSignalhandler();
  }

  inline thread_t * global_getCurrent(void) {
    return current; 
  }

  // Waiting for the stops of threads, no need to hold the lock.
  inline void global_waitThreadsStops(int totalwaiters) {
    global_lockInsideSignalhandler();
    g_waitersTotal = totalwaiters;
//    fprintf(stderr, "During waiting: g_waiters %d g_waitersTotal %d\n", g_waiters, g_waitersTotal);
    while(g_waiters != g_waitersTotal) {
      WRAP(pthread_cond_wait)(&g_condCommitter, &g_mutexSignalhandler);
    }
    global_unlockInsideSignalhandler();
  }

  inline void global_checkWaiters(void) {
    assert(g_waiters == 0);
  }

  // Notify the commiter and wait on the global conditional variable 
  inline void global_waitForNotification(void) {
    assert(global_isEpochEnd() == true);
    
//    printf("waitForNotification, waiters is %d at thread %p\n", g_waiters, pthread_self());
    global_lockInsideSignalhandler();
    PRDBG("waitForNotification g_waiters %d totalWaiters %d\n", g_waiters, g_waitersTotal);

    g_waiters++;

    if(g_waiters == g_waitersTotal) {
      //printf("NNNNNNNNNN, waiters is %d at thread %p. WWWWWWWWWWWWWWWWWWWWWWWWWWW\n", g_waiters, pthread_self());
      WRAP(pthread_cond_signal)(&g_condCommitter); 
      PRDBG("waitForNotification after calling cond_broadcast\n");
    }

    // Only waken up when it is not the end of epoch anymore.
    while(global_isEpochEnd()) {
      PRDBG("waitForNotification before waiting again\n");
      WRAP(pthread_cond_wait)(&g_condWaiters, &g_mutexSignalhandler);
      PRDBG("waitForNotification after waken up. isEpochEnd() %d \n", global_isEpochEnd());
    }

    g_waiters--;

    if(g_waiters == 0) {
      WRAP(pthread_cond_signal)(&g_condCommitter); 
    }
    PRDBG("waitForNotification decrement waiters. status %d\n", g_phase);
      
    global_unlockInsideSignalhandler();
  }
};
#endif
