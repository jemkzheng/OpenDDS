// -*- C++ -*-
//
// $Id$
#include  "DCPS/DdsDcps_pch.h"
#include  "PerConnectionSynch.h"


#if !defined (__ACE_INLINE__)
#include "PerConnectionSynch.inl"
#endif /* __ACE_INLINE__ */


TAO::DCPS::PerConnectionSynch::~PerConnectionSynch()
{
  DBG_ENTRY("PerConnectionSynch","~PerConnectionSynch");
}


void
TAO::DCPS::PerConnectionSynch::work_available()
{
  DBG_ENTRY("PerConnectionSynch","work_available");
  GuardType guard(this->lock_);
  this->work_available_ = 1;
  this->condition_.signal();
}


int
TAO::DCPS::PerConnectionSynch::open(void*)
{
  DBG_ENTRY("PerConnectionSynch","open");
  // Activate this object to start a new thread that will call
  // our svc() method, and then our close() method.
  this->shutdown_ = 0;
  return this->activate(THR_NEW_LWP | THR_JOINABLE, 1);
}


int
TAO::DCPS::PerConnectionSynch::svc()
{
  DBG_ENTRY("PerConnectionSynch","svc");

  ThreadSynchWorker::WorkOutcome work_outcome =
                               ThreadSynchWorker::WORK_OUTCOME_NO_MORE_TO_DO;

  // Loop until we honor the shutdown_ flag.
  while (1)
    {
      VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                 "Top of infinite svc() loop\n"));

      {
        GuardType guard(this->lock_);

        VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                   "Lock acquired.  Check to see what to do next.\n"));

        // We will wait on the condition_ if all of the following are true:
        //
        //   1) The last time the perform_work() method was called, it
        //      indicated that there was no more work to do.
        //   2) Since we last invoked perform_work(), we have not been
        //      informed that there is work_available().
        //   3) We have not been asked to shutdown_ the svc() loop.
        //
        while ((work_outcome ==
                             ThreadSynchWorker::WORK_OUTCOME_NO_MORE_TO_DO) &&
               (this->work_available_ == 0) &&
               (this->shutdown_       == 0))
          {
            VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                       "No work to do.  Just wait on the condition.\n"));
            this->condition_.wait();
            VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                       "We are awake from waiting on the condition.\n"));
          }

        // Maybe we have been asked to shutdown_ the svc() loop.
        if (this->shutdown_ == 1)
          {
            VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                       "Honoring the shutdown request.\n"));
            // We are honoring the request to shutdown_ the svc() loop.
            break;
          }
        // Or, perhaps we experienced a fatal error on our last call to
        // perform_work().
        if (work_outcome == ThreadSynchWorker::WORK_OUTCOME_BROKEN_RESOURCE)
          {
            VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                       "Fatal error - Broken SynchResounce.\n"));
            // Stop the svc() loop.
            break;
          }

        VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                   "Reset our work_available_ flag to 0, and release lock.\n"));

        // Set our work_available_ flag to false (0) before we release the
        // lock so that we will only count any work_available() calls that
        // happen after this point.
        this->work_available_ = 0;
      }

      if (work_outcome == ThreadSynchWorker::WORK_OUTCOME_ClOGGED_RESOURCE)
        {
          // Ask the ThreadSynchResource to block us until the clog situation
          // clears up.
          this->wait_on_clogged_resource();
        }

      VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                 "Call perform_work()\n"));

      // Without the lock, ask the worker to perform some work.  It tells
      // us if it completed with more work to still be performed (or not).
      work_outcome = this->perform_work();

      VDBG((LM_DEBUG,"(%P|%t) DBG:   "
                 "call to perform_work() returned %d\n",work_outcome));
    }

  return 0;
}


int
TAO::DCPS::PerConnectionSynch::close(u_long)
{
  DBG_ENTRY("PerConnectionSynch","close");
  return 0;
}


int
TAO::DCPS::PerConnectionSynch::register_worker_i()
{
  DBG_ENTRY("PerConnectionSynch","register_worker_i");
  return this->open(0);
}


void
TAO::DCPS::PerConnectionSynch::unregister_worker_i()
{
  DBG_ENTRY("PerConnectionSynch","unregister_worker_i");
  // It is at this point that we need to stop the thread that
  // was activated when our open() method was called.
  {
    // Acquire the lock
    GuardType guard(this->lock_);

    // Set the shutdown_ flag to false to shutdown the svc() method loop.
    this->shutdown_ = 1;

    // Signal the condition_ object in case the svc() method is currently
    // blocked wait()'ing on the condition.
    this->condition_.signal();
  }

  // Wait for all threads running this task (there should just be one thread)
  // to finish.
  this->wait();
}