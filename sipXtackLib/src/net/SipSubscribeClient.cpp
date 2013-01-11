//
// Copyright (C) 2010 Avaya Inc., certain elements licensed under a Contributor Agreement.
// Copyright (C) 2007, 2008, 2009 Pingtel Corp., certain elements licensed under a Contributor Agreement.
// Contributors retain copyright to elements licensed under a Contributor Agreement.
// Licensed to the User under the LGPL license.
//
//////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// APPLICATION INCLUDES

#include <os/OsDateTime.h>
#include <os/OsLock.h>
#include <os/OsUnLock.h>
#include <net/SipSubscribeClient.h>
#include <net/SipUserAgent.h>
#include <net/SipDialog.h>
#include <net/SipDialogMgr.h>
#include <net/NetMd5Codec.h>
#include <net/CallId.h>
#include <utl/UtlHashBagIterator.h>

// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS

// msgSubType for an OsMsg indicating that a starting timer
// (SubscriptionGroupState::mStartingTimer) has fired.
#define EVENT_STARTING (OsEventMsg::USER_START)
// msgSubType for an OsMsg indicating that a restart timer
// (SubscriptionGroupState::mRestartTimer) has fired.
#define EVENT_RESTART (OsEventMsg::USER_START + 1)

// We initially allow SUBSCRIPTION_STARTUP_INITIAL secs. for a
// subscription to start up.  If that fials, we successively double
// the time allowed until either success, or the startup time would
// exceed SUBSCRIPTION_STARTUP_MAX (after which the wait interval
// remains unchanged).
#define SUBSCRIPTION_STARTUP_INITIAL 15
#define SUBSCRIPTION_STARTUP_MAX (5 * 60)

/// Basic subscription restart interval, in secs.
// See SubscriptionGroupState::setRestartTimer() for how the restart interval
// is actually calculated.
#define RESTART_INTERVAL  (24 * 60 * 60)

// STATIC VARIABLE INITIALIZATIONS

/// OsMsg subclass for the firing of mStartingTimer.
// Check whether the subscription has been started successfully.
class StartingEventMsg : public OsMsg
{
   /* //////////////////////////// PUBLIC //////////////////////////////////// */
public:

   /* ============================ CREATORS ================================== */

   StartingEventMsg(const UtlString& handle /**< handle of the SubscriptionGroupState
                                             *  to restart */
      ) :
      OsMsg(OS_EVENT, EVENT_STARTING),
      mHandle(handle)
      {
      }

   virtual
   ~StartingEventMsg()
      {
      }
   //:Destructor

   /* ============================ MANIPULATORS ============================== */

   virtual OsMsg* createCopy(void) const
      {
         return new StartingEventMsg(mHandle);
      };
   //:Create a copy of this msg object (which may be of a derived type)

   /* ============================ ACCESSORS ================================= */

   // Get pointer to the handle value.
   UtlString* getHandle()
      {
         return &mHandle;
      }

   /* ============================ INQUIRY =================================== */

   /* //////////////////////////// PROTECTED ///////////////////////////////// */
protected:

   /* //////////////////////////// PRIVATE /////////////////////////////////// */
private:

   StartingEventMsg(const StartingEventMsg& rStartingEventMsg);
   //:Copy constructor (not implemented for this class)

   StartingEventMsg& operator=(const StartingEventMsg& rhs);
   //:Assignment operator (not implemented for this class)

   /// Handle of the SubscriptionGroupState to check for successful starting.
   UtlString mHandle;

};


/// OsMsg subclass for the firing of mRestartTimer - Subscription should be restarted.
class RestartEventMsg : public OsMsg
{
   /* //////////////////////////// PUBLIC //////////////////////////////////// */
public:

   /* ============================ CREATORS ================================== */

   RestartEventMsg(const UtlString& handle /**< handle of the SubscriptionGroupState
                                             *  to restart */
      ) :
      OsMsg(OS_EVENT, EVENT_RESTART),
      mHandle(handle)
      {
      }

   virtual
   ~RestartEventMsg()
      {
      }
   //:Destructor

   /* ============================ MANIPULATORS ============================== */

   virtual OsMsg* createCopy(void) const
      {
         return new RestartEventMsg(mHandle);
      };
   //:Create a copy of this msg object (which may be of a derived type)

   /* ============================ ACCESSORS ================================= */

   // Get pointer to the handle value.
   UtlString* getHandle()
      {
         return &mHandle;
      }

   /* ============================ INQUIRY =================================== */

   /* //////////////////////////// PROTECTED ///////////////////////////////// */
protected:

   /* //////////////////////////// PRIVATE /////////////////////////////////// */
private:

   RestartEventMsg(const RestartEventMsg& rRestartEventMsg);
   //:Copy constructor (not implemented for this class)

   RestartEventMsg& operator=(const RestartEventMsg& rhs);
   //:Assignment operator (not implemented for this class)

   /// Handle of the SubscriptionGroupState to restart.
   UtlString mHandle;

};

/* //////////////////////////// PUBLIC //////////////////////////////////// */

/* ============================ CREATORS ================================== */

// Constructor for private class
SipSubscribeClient::SubscriptionGroupState::SubscriptionGroupState(SipSubscribeClient* pClient,
                                               const UtlString& handle,
                                               SipMessage* pSubscriptionRequest,
                                               void* pApplicationData,
                                               SipSubscribeClient::SubscriptionStateCallback pStateCallback,
                                               SipSubscribeClient::NotifyEventCallback pNotifyCallback,
                                               bool reestablish,
                                               bool restart) :
   UtlString(handle),
   mpSubscriptionRequest(pSubscriptionRequest),
   mCurrentEarlyDialogHandle(handle),
   mpApplicationData(pApplicationData),
   mpStateCallback(pStateCallback),
   mpNotifyCallback(pNotifyCallback),
   mReestablish(reestablish),
   mRestart(restart),
   mStarting(false),
   mStartingTimeout(-1),
   mStartingTimer(new StartingEventMsg(static_cast <const UtlString&> (*this)),
                 pClient->getMessageQueue()),
   mRestartTimer(new RestartEventMsg(static_cast <const UtlString&> (*this)),
                 pClient->getMessageQueue())
{
}

// Destructor for private class
SipSubscribeClient::SubscriptionGroupState::~SubscriptionGroupState()
{
   // Free the saved SUBSCRIBE request.
   delete mpSubscriptionRequest;
}

// Debug dump of SubscriptionGroupState.
void SipSubscribeClient::SubscriptionGroupState::toString(UtlString& dumpString)
{
   dumpString = "SubscriptionGroupState:\n";
   dumpString.append("\nOriginal handle: ");
   dumpString.append(*this);
   dumpString.append("\nCurrent handle: ");
   dumpString.append(mCurrentEarlyDialogHandle);
   dumpString.append("\nmpApplicationData: ");
   char string[20];
   sprintf(string, "%p", mpApplicationData);
   dumpString.append(string);
   dumpString.append("\nmpStateCallback: ");
   sprintf(string, "%p", mpStateCallback);
   dumpString.append(string);
   dumpString.append("\nmpNotifyCallback: ");
   sprintf(string, "%p", mpNotifyCallback);
   dumpString.append(string);
   dumpString.append('\n');
   dumpString.append("\nmReestablish: ");
   sprintf(string, "%d", mReestablish);
   dumpString.append(string);
   dumpString.append('\n');
   dumpString.append("\nmRestart: ");
   sprintf(string, "%d", mRestart);
   dumpString.append(string);
   dumpString.append('\n');
}

void SipSubscribeClient::SubscriptionGroupState::lock()
{
    _accessMutex.lock();
}

void SipSubscribeClient::SubscriptionGroupState::unlock()
{
    _accessMutex.unlock();
}

template <class T> Wrap<T>::Wrap()
{
    _obj = T();
}

template <class T> Wrap<T>::Wrap(T obj)
{
    _obj = obj;

    if (_obj)
    {
        _obj->lock();
    }
}

template <class T> Wrap<T>::~Wrap()
{
    if (_obj)
    {
        _obj->unlock();
    }
}

//! Copying operators.
//SubscriptionGroupState::SubscriptionGroupState(const SubscriptionGroupState& rSubscriptionGroupState)

//SubscriptionGroupState& SubscriptionGroupState::operator=(const SubscriptionGroupState& rhs)

/* //////////////////////////// PUBLIC //////////////////////////////////// */

/* ============================ CREATORS ================================== */

// Constructor for private class
SipSubscribeClient::SubscriptionDialogState::SubscriptionDialogState(const UtlString& handle,
                                                 SubscriptionGroupState::Ptr pGroupState,
                                                 SipSubscribeClient::SubscriptionState state) :
   UtlString(handle),
   mpGroupState(pGroupState),
   mState(state),
   mNotifyReceived(false)
{
}

// Destructor for private class
SipSubscribeClient::SubscriptionDialogState::~SubscriptionDialogState()
{
}

// Debug dump of SipSubscribeClient::SubscriptionDialogState.
void SipSubscribeClient::SubscriptionDialogState::toString(UtlString& dumpString)
{
    dumpString = "SipSubscribeClient::SubscriptionDialogState:\n";
    dumpString.append("\nmpHandle: ");
    dumpString.append(*this);
    dumpString.append("\nmState: ");
    UtlString subStateString;
    SipSubscribeClient::getSubscriptionStateEnumString(mState, subStateString);
    dumpString.append(subStateString);
}

void SipSubscribeClient::SubscriptionDialogState::lock()
{
    _accessMutex.lock();
}

void SipSubscribeClient::SubscriptionDialogState::unlock()
{
    _accessMutex.unlock();
}

//! Copying operators.
//SipSubscribeClient::SubscriptionDialogState::SipSubscribeClient::SubscriptionDialogState(const SipSubscribeClient::SubscriptionDialogState& rSipSubscribeClient::SubscriptionDialogState)

//SipSubscribeClient::SubscriptionDialogState& SipSubscribeClient::SubscriptionDialogState::operator=(const SipSubscribeClient::SubscriptionDialogState& rhs)


// Constructor
SipSubscribeClient::SipSubscribeClient(SipUserAgent& userAgent,
                                       SipDialogMgr& dialogMgr,
                                       SipRefreshManager& refreshManager)
    : OsServerTask("SipSubscribeClient-%d")
    , mpUserAgent(&userAgent)
    , mpDialogMgr(&dialogMgr)
    , mpRefreshManager(&refreshManager)
    , mSemaphore(OsBSem::Q_FIFO, OsBSem::FULL)
{
}

// Copy constructor
//SipSubscribeClient::SipSubscribeClient(const SipSubscribeClient& rSipSubscribeClient)

// Destructor
SipSubscribeClient::~SipSubscribeClient()
{
    // Do not delete mpUserAgent, mpDialogMgr or mpRefreshManager.  They
    // may be used elsewhere and need to be deleted outside the
    // SipSubscribeClient.

    // Stop receiving NOTIFY requests.
    mpUserAgent->removeMessageObserver(*(getMessageQueue()));

    // Wait until this OsServerTask has stopped or handleMethod
    // might access something we are about to delete here.
    waitUntilShutDown();

    // Delete the event type strings
    mEventTypes.destroyAll();

    // Unsubscribe to anything that is in the list
    endAllSubscriptions();
    // mSubscriptions should now be empty
}

/* ============================ MANIPULATORS ============================== */

// Assignment operator
//SipSubscribeClient&
//SipSubscribeClient::operator=(const SipSubscribeClient& rhs)

UtlBoolean SipSubscribeClient::addSubscription(
   const char* resourceId,
   const char* eventHeaderValue,
   const char* acceptHeaderValue,
   const char* fromFieldValue,
   const char* toFieldValue,
   const char* contactFieldValue,
   int subscriptionPeriodSeconds,
   void* applicationData,
   const SubscriptionStateCallback subscriptionStateCallback,
   const NotifyEventCallback notifyEventsCallback,
   UtlString& earlyDialogHandle,
   bool reestablish,
   bool restart)
{
    UtlString callId;
    CallId::getNewCallId(callId);

    // Construct a SUBSCRIBE request
    SipMessage* subscribeRequest = new SipMessage;
    subscribeRequest->setSubscribeData(resourceId,
                                       fromFieldValue,
                                       toFieldValue,
                                       callId,
                                       1, // cseq
                                       eventHeaderValue,
                                       acceptHeaderValue,
                                       NULL, // Event header id parameter
                                       contactFieldValue,
                                       NULL, // initial request no routeField
                                       subscriptionPeriodSeconds);

    // Create a subscription from the request.
    return addSubscription(subscribeRequest,
                           applicationData,
                           subscriptionStateCallback,
                           notifyEventsCallback,
                           earlyDialogHandle,
                           reestablish,
                           restart);
}

UtlBoolean SipSubscribeClient::addSubscription(
   SipMessage* subscriptionRequest,
   void* applicationData,
   const SubscriptionStateCallback subscriptionStateCallback,
   const NotifyEventCallback notifyEventsCallback,
   UtlString& earlyDialogHandle,
   bool reestablish,
   bool restart)
{
    // Set the from tag if it is not already set.
    Url fromUrl;
    subscriptionRequest->getFromUrl(fromUrl);
    UtlString fromTag;
    fromUrl.getFieldParameter("tag", fromTag);
    if (fromTag.isNull())
    {
        UtlString fromFieldValue;
        fromUrl.toString(fromFieldValue);
        CallId::getNewTag(fromTag);
        fromUrl.setFieldParameter("tag", fromTag);
        fromUrl.toString(fromFieldValue);
        subscriptionRequest->setRawFromField(fromFieldValue);
    }

    // Get the event type and make sure we are registered to
    // receive NOTIFY requests for this event type.
    UtlString eventType;
    subscriptionRequest->getEventFieldParts(&eventType);
    // If this event type is not in the list, we need to register
    // to receive NOTIFY requests for this event type.
    {
       OsLock lock(mSemaphore);

       if (mEventTypes.find(&eventType) == NULL)
       {
          // receive NOTIFY requests for this event type
          mpUserAgent->addMessageObserver(*(getMessageQueue()),
                                          SIP_NOTIFY_METHOD,
                                          TRUE, // yes requests
                                          FALSE, // no responses
                                          TRUE, // incoming,
                                          FALSE, // outgoing
                                          eventType);

          // Note: we do not register to receive SUBSCRIBE responses
          // as the refreshManager will do that and invoke the
          // SubScribeClient's callback.

          // Add this event type to the list so we know we
          // have registered for NOTIFY requests for this event type.
          mEventTypes.insert(new UtlString(eventType));
       }
    }

    // Create a SubscriptionGroupState and set the members.

    UtlString handle;
    subscriptionRequest->getDialogHandle(handle);
    SubscriptionGroupState::Ptr groupState =
            SubscriptionGroupState::Ptr(new SubscriptionGroupState(this,
                                  handle,
                                  new SipMessage(*subscriptionRequest),
                                  applicationData,
                                  subscriptionStateCallback,
                                  notifyEventsCallback,
                                  reestablish,
                                  restart));

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "SipSubscribeClient::addSubscription adding subscription with handle '%s'",
                  handle.data());

    // Start the timer that checks for successful creation of
    // subscription dialog(s).
    groupState->setStartingTimer(true);

    // Start the timer that periodically reestablishes the subscription.
    if (restart)
    {
       groupState->setRestartTimer();
    }

    // Put the state in the list
    addGroupState(groupState);

    // Give a copy of the request to the refresh manager to send the
    // SUBSCRIBE and keep the subscription alive.
    UtlBoolean initialSendOk =
       mpRefreshManager->initiateRefresh(subscriptionRequest,
                                         //< give ownership to mpRefreshManager
                                         this,
                                         // refreshCallback receives 'this' as app. data
                                         SipSubscribeClient::refreshCallback,
                                         earlyDialogHandle);

    // The handle returned by initiateRefresh should be the same as the one
    // we computed above.
    if (handle.compareTo(earlyDialogHandle) != 0)
    {
       Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                     "SipSubscribeClient::addSubscription handle problem: handle = '%s', earlyDialogHandle = '%s'",
                     handle.data(), earlyDialogHandle.data());
    }
    assert(handle.compareTo(earlyDialogHandle) == 0);

    return initialSendOk;
}

UtlBoolean SipSubscribeClient::endSubscriptionGroup(const UtlString& earlyDialogHandle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::endSubscriptionGroup earlyDialogHandle = '%s'",
                 earlyDialogHandle.data());

   Wrap<SubscriptionGroupState::Ptr> groupState;
   {
      groupState = removeGroupStateByOriginalHandle(earlyDialogHandle);

      // Ensure that terminated subscriptions do not cause restart of the
      // subscription.
      if (groupState)
      {
         groupState->mReestablish = false;
         groupState->mRestart = false;
      }

      // Update the SipRefreshManager while we are locking the SipSubscribeClient
      // to ensure that the two are synchronized.
      // Stop the refresh of all dialogs and unsubscribe.
      mpRefreshManager->stopRefresh(earlyDialogHandle);

      // At this point, we have the only pointer to *groupState, so we do not
      // need to hold the lock to prevent other threads from deleting it while
      // we are accessing *groupState.
   }

   int count = 0;
   if (groupState)
   {
      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::endSubscriptionGroup groupState = %s",
                    groupState->data());

      // Stop the timers, synchronously.
      // Since mReestablish and mRestart are now false, this cannot cause
      // further actions.
      groupState->mStartingTimer.stop(TRUE);
      groupState->mRestartTimer.stop(TRUE);

      Wrap<SubscriptionDialogState::Ptr> dialogState;

      // Repeatedly get the key of the first subscription in mSubscriptionDialogs
      // that is within the group and terminate that subscription.
      while (1)
      {
        dialogState = getDialogStateByGroupState(groupState.get());
        if (dialogState)
        {
            endSubscriptionDialog(dialogState->data());
            count++;
        }
        else
        {
            break;
        }
      }

      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::endSubscriptionGroup exited %d dialogs",
                    count);

      groupState = SubscriptionGroupState::Ptr();
   }

   return (count > 0);
}

UtlBoolean SipSubscribeClient::endSubscriptionDialog(const UtlString& dialogHandle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::endSubscriptionDialog dialogHandle = '%s'",
                 dialogHandle.data());

   Wrap<SubscriptionDialogState::Ptr> dialogState;
   {
      // Delete the dialogState so that when the termination NOTIFY arrives
      // it does not match an existing dialog and so does not cause the
      // subscription to be reestablished.
      dialogState = removeDialogState(dialogHandle);


      /* TODO: Investigate if this lock is needed anymore */
      OsLock lock(mSemaphore);
      // Update the SipRefreshManager while we are locking the SipSubscribeClient
      // to ensure that the two are synchronized.
      // Stop the refresh and unsubscribe
      mpRefreshManager->stopRefresh(dialogHandle.data());
   }

   if (dialogState)
   {
      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::endSubscriptionDialog dialogState = %p",
                    dialogState.get().get());

      // If there is a state change and there is a callback function
      if (dialogState->mState != SUBSCRIPTION_TERMINATED &&
          dialogState->mpGroupState->mpStateCallback)
      {
         // Indicate that the subscription was terminated
         dialogState->mpGroupState->
            mpStateCallback(SUBSCRIPTION_TERMINATED,
                            dialogState->mpGroupState->data(),
                            dialogState->data(),
                            dialogState->mpGroupState->mpApplicationData,
                            -1, // no response code
                            NULL, // no response text
                            0, // expires now
                            NULL); // no response
      }
   }

   return dialogState.get() != NULL;
}

UtlBoolean SipSubscribeClient::endSubscriptionDialogByNotifier(const UtlString& dialogHandle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::endSubscriptionDialogByNotifier dialogHandle = '%s'",
                 dialogHandle.data());

   Wrap<SubscriptionDialogState::Ptr> dialogState;
   {
      // Delete the dialogState so that when the termination NOTIFY arrives
      // it does not match an existing dialog and so does not cause the
      // subscription to be reestablished.
      dialogState = removeDialogState(dialogHandle);

      OsLock  lock(mSemaphore);
      // Update the SipRefreshManager while we are locking the SipSubscribeClient
      // to ensure that the two are synchronized.
      // Stop the refresh but do not send an un-SUBSCRIBE, as the subscription
      // has already been ended by the notifier.
      mpRefreshManager->stopRefresh(dialogHandle.data(), TRUE);
   }

   if (dialogState)
   {
      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::endSubscriptionDialogByNotifier dialogState = %p",
                    dialogState.get().get());

      // If there is a state change and there is a callback function
      if (dialogState->mState != SUBSCRIPTION_TERMINATED &&
          dialogState->mpGroupState->mpStateCallback)
      {
         // Indicate that the subscription was terminated
         dialogState->mpGroupState->
            mpStateCallback(SUBSCRIPTION_TERMINATED,
                            dialogState->mpGroupState->data(),
                            dialogState->data(),
                            dialogState->mpGroupState->mpApplicationData,
                            -1, // no response code
                            NULL, // no response text
                            0, // expires now
                            NULL); // no response
      }
   }

   return dialogState.get() != NULL;
}

void SipSubscribeClient::endAllSubscriptions()
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::endAllSubscriptions entered");

   int count = 0;

   // Repeatedly get the key of the first subscription in mSubscriptionGroups
   // and terminate that subscription.
   while (1)
   {
       Wrap<SubscriptionGroupState::Ptr> groupState;
       SubscriptionGroupState::Ptr groupStatePtr;

       {
           OsLock lock(mSemaphore);

           if (mSubscriptionGroups.empty())
           {
               break;
           }

           groupStatePtr = *mSubscriptionGroups.begin();
       }

       groupState = Wrap<SubscriptionGroupState::Ptr>(groupStatePtr);

       endSubscriptionGroup(groupState->data());
       count++;
   }

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::endAllSubscriptions exited %d entries",
                 count);
}

UtlBoolean SipSubscribeClient::changeSubscriptionTime(const char* earlyDialogHandle, int subscriptionPeriodSeconds)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::changeSubscriptionTime dialogHandle = '%s', timeout = %d",
                 earlyDialogHandle, subscriptionPeriodSeconds);

    // Give the request to the refresh manager to send the
    // subscribe and keep the subscription alive
    return mpRefreshManager->changeRefreshTime(earlyDialogHandle, subscriptionPeriodSeconds);
}

UtlBoolean SipSubscribeClient::handleMessage(OsMsg &eventMessage)
{
    int msgType = eventMessage.getMsgType();
    int msgSubType = eventMessage.getMsgSubType();

    // SIP message
    if (msgType == OsMsg::PHONE_APP &&
        msgSubType == SipMessage::NET_SIP_MESSAGE)
    {
        const SipMessage* sipMessage = ((SipMessageEvent&)eventMessage).getMessage();

        // If this is a NOTIFY request
        if (sipMessage)
        {
           UtlString method;
           sipMessage->getRequestMethod(&method);
           if (method.compareTo(SIP_NOTIFY_METHOD) == 0 &&
               !sipMessage->isResponse())
           {
              handleNotifyRequest(*sipMessage);
           }
           else
           {
              Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                            "SipSubscribeClient::handleMessage unexpected %s %s",
                            method.data(),
                            sipMessage->isResponse() ? "response" : "request");
           }
        }
        else
        {
            Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                "SipSubscribeClient::handleMessage  SipMessageEvent with NULL SipMessage");
        }
    }
    // Starting timer fired - Check subscription to see if it started.
    else if (msgType == OsMsg::OS_EVENT &&
             msgSubType == EVENT_STARTING)
    {
       // Call handleStartingEvent on the handle.
       StartingEventMsg* m = dynamic_cast <StartingEventMsg*> (&eventMessage);
       assert(m != 0);
       handleStartingEvent(*m->getHandle());
    }
    // Restart timer fired - Subscription should be restarted.
    else if (msgType == OsMsg::OS_EVENT &&
             msgSubType == EVENT_RESTART)
    {
       // Call handleRestartEvent on the handle.
       RestartEventMsg* m = dynamic_cast <RestartEventMsg*> (&eventMessage);
       assert(m != 0);
       handleRestartEvent(*m->getHandle());
    }

    return (TRUE);
}

/* ============================ ACCESSORS ================================= */

int SipSubscribeClient::countSubscriptionGroups()
{
   OsLock lock(mSemaphore);

   int count = mSubscriptionGroups.size();

   return count;
}

int SipSubscribeClient::countSubscriptionDialogs()
{
   OsLock lock(mSemaphore);

   int count = mSubscriptionDialogs.size();

   return count;
}

void SipSubscribeClient::getSubscriptionStateEnumString(enum SubscriptionState stateValue,
                                                        UtlString& stateString)
{
    switch(stateValue)
    {
    case SUBSCRIPTION_INITIATED: // Early dialog
        stateString = "SUBSCRIPTION_INITIATED";
        break;

    case SUBSCRIPTION_SETUP:     // Established dialog
        stateString = "SUBSCRIPTION_SETUP";
        break;

    case SUBSCRIPTION_TERMINATED: // Failed during setup or refresh, or ended by application.
        stateString = "SUBSCRIPTION_TERMINATED";
        break;

    default:
        {
            char stateNum[20];
            sprintf(stateNum, "%d", stateValue);
            stateString = "INVALID: ";
            stateString.append(stateNum);
        }
        break;
    }
}

/* ============================ INQUIRY =================================== */

// Dump the object's internal state.
void SipSubscribeClient::dumpState()
{
   OsLock lock(mSemaphore);

   // indented 2

   Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                 "\t  SipSubscribeClient %p",
                 this);

   UtlString s;

   std::vector<SubscriptionGroupState::Ptr>::iterator groupIterator;

   SubscriptionGroupState::Ptr groupState;
   for (groupIterator = mSubscriptionGroups.begin(); groupIterator != mSubscriptionGroups.end(); groupIterator++)
   {
       groupState = *groupIterator;

      groupState->toString(s);
      Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                    "\t    SubscribeGroupState %p %s",
                    groupState.get(), s.data());

      std::vector<SubscriptionDialogState::Ptr>::iterator dialogIterator;
      SubscriptionDialogState::Ptr dialogState;
      for (dialogIterator = mSubscriptionDialogs.begin(); dialogIterator != mSubscriptionDialogs.end(); dialogIterator++)
      {
          dialogState = *dialogIterator;

          dialogState->toString(s);
          Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                        "\t      SubscribeDialogState %p %s",
                        dialogState.get(), s.data());
      }
   }
   mpRefreshManager->dumpState();
}

void SipSubscribeClient::dumpSubscriptionDialog(const char *message, SubscriptionDialogState::Ptr &dialogState)
{
    return ;
    UtlString s;
      dialogState->toString(s);
      Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                    "QQQ %s   SubscribeDialogState %p %s REF=%d",
                    message, dialogState.get(), s.data(), dialogState.use_count());
}

void SipSubscribeClient::dumpSubscriptionDialogs(const char *message, bool lock)
{
    return;
    Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                  "QQQ\t      SubscribeDialogState DUMP %s", message);

    if (lock)
    {
   OsLock lock(mSemaphore);

   UtlString s;

  std::vector<SubscriptionDialogState::Ptr>::iterator dialogIterator;
  for (dialogIterator = mSubscriptionDialogs.begin(); dialogIterator != mSubscriptionDialogs.end(); dialogIterator++)
  {
      SubscriptionDialogState::Ptr dialogState = *dialogIterator;

      dialogState->toString(s);
      Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                    "QQQ\t      SubscribeDialogState %p %s ref=%d",
                    dialogState.get(), s.data(), dialogState.use_count());
  }
    }
    else
    {
        UtlString s;

       std::vector<SubscriptionDialogState::Ptr>::iterator dialogIterator;
       for (dialogIterator = mSubscriptionDialogs.begin(); dialogIterator != mSubscriptionDialogs.end(); dialogIterator++)
       {
           SubscriptionDialogState::Ptr dialogState = *dialogIterator;

           dialogState->toString(s);
           Os::Logger::instance().log(FAC_RLS, PRI_INFO,
                         "QQQ\t      SubscribeDialogState %p %s ref=%d",
                         dialogState.get(), s.data(), dialogState.use_count());
       }
    }
}


/* //////////////////////////// PROTECTED ///////////////////////////////// */

void SipSubscribeClient::refreshCallback(SipRefreshManager::RefreshRequestState newState,
                                         const char* earlyDialogHandle,
                                         const char* dialogHandle,
                                         void* subscribeClientPtr,
                                         int responseCode,
                                         const char* responseText,
                                         long expirationDate, // epoch seconds
                                         const SipMessage* subscribeResponse)
{
   ((SipSubscribeClient*) subscribeClientPtr)->
      refreshCallback(newState,
                      earlyDialogHandle,
                      dialogHandle,
                      responseCode,
                      responseText,
                      expirationDate,
                      subscribeResponse);
}

void SipSubscribeClient::refreshCallback(SipRefreshManager::RefreshRequestState newState,
                                         const char* earlyDialogHandle,
                                         const char* dialogHandle,
                                         int responseCode,
                                         const char* responseText,
                                         long expirationDate, // epoch seconds
                                         const SipMessage* subscribeResponse)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::refreshCallback "
                 "newState = %d (%s), earlyDialogHandle = '%s', dialogHandle = '%s', "
                 "responseCode = %d, responseText = '%s', expirationDate = %ld",
                 newState, SipRefreshManager::refreshRequestStateText(newState),
                 earlyDialogHandle, dialogHandle, responseCode,
                 responseText, expirationDate);

   switch (newState)
   {
      // Do nothing for early dialog for subscribes
   case SipRefreshManager::REFRESH_REQUEST_PENDING:
      break;

   case SipRefreshManager::REFRESH_REQUEST_SUCCEEDED:
   { // Variable scope
      // A subscription received a successful response.
      // We may already have dialog state for it, or we may not.

      // Find the subscription group.
      Wrap<SubscriptionGroupState::Ptr> groupState = getGroupStateByCurrentHandle(earlyDialogHandle);
      if (groupState)
      {
          {
              // Increment the count of success responses.
              groupState->mSuccessResponses++;
          }

         Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                       "SipSubscribeClient::refreshCallback "
                       "for group '%s', incrementing mSuccessResponses to %d",
                       groupState->data(),
                       groupState->mSuccessResponses);

         // It would be convenient to not have to create the
         // SipSubscribeClient::SubscriptionDialogState here (as RFC 3265bis does not
         // recognize a dialog to have been created until a NOTIFY
         // arrives), but we need to keep the set of
         // SipSubscribeClient::SubscriptionDialogState's aligned with the state of the
         // SipDialogMgr and SipRefreshManager.

         // Find the subscription dialog.
         Wrap<SubscriptionDialogState::Ptr> dialogState = getDialogState(dialogHandle);
         if (!dialogState)
         {
            // Subscription dialog state does not exist -- create and insert it.

            UtlString d(dialogHandle);
            SubscriptionDialogState::Ptr dialogStatePtr =
                        SubscriptionDialogState::Ptr(new SubscriptionDialogState(d, groupState.get(), SUBSCRIPTION_SETUP));
            dialogState = Wrap<SubscriptionDialogState::Ptr>(dialogStatePtr);
            addDialogState(dialogStatePtr);

            {
                // Increment the count of success responses.
                groupState->mSuccessResponses++;
            }

            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "SipSubscribeClient::refreshCallback "
                          "for group '%s', incrementing mSuccessResponses to %d",
                          groupState->data(),
                          groupState->mSuccessResponses);
         }

         if (groupState->mpStateCallback)
         {
            // Do the callback with the new expiration time.
            groupState->mpStateCallback(dialogState->mState,
                                        groupState->data(),
                                        dialogState->data(),
                                        groupState->mpApplicationData,
                                        responseCode,
                                        responseText,
                                        expirationDate,
                                        subscribeResponse);
         }
      }
      else
      {
         Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                       "SipSubscribeClient::refreshCallback No subscription group state could be found for early dialog handle '%s'",
                       earlyDialogHandle);
      }
   }
   break;

   case SipRefreshManager::REFRESH_REQUEST_FAILED:
   { // Variable scope
      // Early or established dialog failed.  If the dialog was
      // established and the subscription had not expired yet
      // we would have recieved a DIALOG_ESTABLISHED with an
      // error response code.  So if the state is DIALOG_FAILED,
      // either the early dialog failed, or the subscription
      // has expired and the reSUBSCRIBE failed.  We do not hear
      // about SUBSCRIBEs that fail due to authorization unless
      // there are no matching credentials or the credentials did
      // not work.

      // Tell the Refresh Manager to delete the state for this dialog.
      mpRefreshManager->stopRefresh(dialogHandle ?
                                    dialogHandle :
                                    earlyDialogHandle);

      // Find the subscription group.
      Wrap<SubscriptionGroupState::Ptr> groupState = getGroupStateByCurrentHandle(earlyDialogHandle);
      if (groupState)
      {
          {
         // Increment the count of failure responses.
         groupState->mFailureResponses++;
         Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                       "SipSubscribeClient::refreshCallback "
                       "for group '%s', incrementing mFailureResponses to %d",
                       groupState->data(),
                       groupState->mFailureResponses);
          }

         // Find the subscription dialog.
         Wrap<SubscriptionDialogState::Ptr> dialogState = getDialogState(dialogHandle);

         // If an initial SUBSCRIBE fails, we can get a FAILED callback
         // for a dialog we do not yet have state for.  In that case,
         // we have not notified our application to say that the dialog exists,
         // so we need not notify out application that the dialog has failed.
         if (dialogState)
         {
            if (dialogState->mState != SUBSCRIPTION_TERMINATED)
            {
               // Mark the subscription as failed.
               dialogState->mState = SUBSCRIPTION_TERMINATED;

               if (groupState->mpStateCallback)
               {
                  // Do the callback with the new state.
                  groupState->mpStateCallback(dialogState->mState,
                                              groupState->data(),
                                              dialogState->data(),
                                              groupState->mpApplicationData,
                                              responseCode,
                                              responseText,
                                              expirationDate,
                                              subscribeResponse);
               }
            }
         }
         else
         {
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "SipSubscribeClient::refreshCallback No subscription dialog state could be found for dialog handle '%s'",
                          earlyDialogHandle);
         }

         // If the subscription is established, and mReestablish is
         // set, reestablish the subscription.
         if (!groupState->mStarting && groupState->mReestablish)
         {
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "SipSubscribeClient::refreshCallback REFRESH_REQUEST_FAILED triggering reestablishment for group '%s'",
                          earlyDialogHandle);

            UtlString originalHandle(groupState->data());

            groupState = SubscriptionGroupState::Ptr();
            dialogState = Wrap<SubscriptionDialogState::Ptr>();

            reestablish(originalHandle);
         }
      }
      else
      {
         Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                       "SipSubscribeClient::refreshCallback No subscription group state could be found for early dialog handle '%s'",
                       earlyDialogHandle);
      }
   }
   break;

   // This should not happen
   case SipRefreshManager::REFRESH_REQUEST_UNKNOWN:
   default:
      Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                    "SipSubscribeClient::refreshCallback invalid dialog state change: %d",
                    newState);
      break;
   }
}

/* //////////////////////////// PRIVATE /////////////////////////////////// */

void SipSubscribeClient::handleNotifyRequest(const SipMessage& notifyRequest)
{
    UtlString eventField;
    notifyRequest.getEventFieldParts(&eventField);
    // We could validate that the event field is
    // set and is the right event type, but mostly
    // we should not care as we know the event type
    // from the subscription.  We can be tolerant of
    // missing or malformed event headers in the
    // NOTIFY request.  However this does not support
    // multiple event types in the same dialog

    // Determine if the NOTIFY contains "Subscription-State: terminated".
    UtlBoolean terminated = FALSE;
    // Determine if the NOTIFY contains an "expires" parameter in
    // the Subscription-State header.
    int expires = -1;           // -1 means no value found

    const char* subscription_state =
       notifyRequest.getHeaderValue(0, SIP_SUBSCRIPTION_STATE_FIELD);
    if (subscription_state)
    {
       // Examine the state value.
       #define TERMINATED "terminated"
       terminated = strncasecmp(subscription_state, TERMINATED,
                                sizeof (TERMINATED) - 1) == 0;

       // Parse the 'expires' parameter.
       #define EXPIRES "expires"
       const char* p = strcasestr(subscription_state, EXPIRES);
       if (p)
       {
          // If so, extract the expriation interval.
          p += sizeof (EXPIRES) - 1;
          while (isspace(*p))
          {
             p++;
          }
          if (*p == '=')
          {
             p++;
             while (isspace(*p))
             {
                p++;
             }
             expires = atoi(p);
             if (expires <= 0)
             {
                // Negative values are invalid.
                // Zero value is probably due to an error and cannot be
                // obeyed, anyway.
                expires = -1;
             }
          }
       }
    }
    if (!terminated && expires == -1)
    {
       UtlString s;
       ssize_t l;
       notifyRequest.getBytes(&s, &l);

       Os::Logger::instance().log(FAC_SIP, PRI_WARNING,
                     "Received NOTIFY with ongoing state ('%s') but no/invalid expires value (%d): %s",
                     subscription_state, expires, s.data());
    }

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
		  "SipSubscribeClient::handleNotifyRequest "
		  "subscription_state = '%s', terminated = %d, expires = %d",
		  subscription_state, terminated, expires);

    // Determine and update the state of the subscription dialog.
    // This is complicated by the fact that the state is represented in
    // two places (*mpDialogMgr/*mpRefreshManager and
    // mSubscriptionGroups/mSubscriptionDialogs), and they may not agree.
    UtlBoolean foundDialog = false;
    UtlBoolean foundEarlyDialog = false;
    enum SipDialogMgr::transactionSequence sequence = SipDialogMgr::NO_DIALOG;
    Wrap<SubscriptionDialogState::Ptr> dialogState;
    Wrap<SubscriptionGroupState::Ptr> groupState;

    UtlString notifyDialogHandle;
    notifyRequest.getDialogHandle(notifyDialogHandle);

    // Is there an established dialog?
    foundDialog = mpDialogMgr->dialogExists(notifyDialogHandle);

    // Is there an early dialog?
    // Even if there is an established dialog, we still need
    // the early dialog handle to pass to the callback
    // routines.
    // Construct the early dialog handle, keeping in mind that the
    // tags on the NOTIFY are the opposite of what they are on
    // the SUBSCRIBE
    // (Can't use SipDialogMgr methods here because we may be looking for
    // an established dialog that is sibling to this NOTIFY, and there is
    // no method for that.)
    UtlString earlyDialogHandle(notifyDialogHandle);
    ssize_t comma1 = earlyDialogHandle.index(',');
    ssize_t comma2 = earlyDialogHandle.index(',', comma1+1);
    earlyDialogHandle.remove(comma1, comma2-comma1);
    earlyDialogHandle.append(',');
    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "SipSubscribeClient::handleNotifyRequest notifyDialogHandle = '%s', earlyDialogHandle = '%s'",
                  notifyDialogHandle.data(), earlyDialogHandle.data());

    foundEarlyDialog = mpDialogMgr->earlyDialogExists(earlyDialogHandle);

    if (foundDialog)
    {
       groupState = getGroupStateByCurrentHandle(earlyDialogHandle);
       if (groupState)
       {
          // Set the sequence status from SipDialogMgr.
          sequence =
             mpDialogMgr->isNewRemoteTransaction(notifyRequest);

          // Get or create the dialog state.
	  // (If this fails to find a dialog state, one will be created below.)
          dialogState = getDialogState(notifyDialogHandle);
       }
       else
       {
          sequence = SipDialogMgr::NO_DIALOG;
          Os::Logger::instance().log(FAC_SIP, PRI_WARNING,
                        "SipSubscribeClient::handleNotifyRequest No subscription group state found for handle '%s'",
                        earlyDialogHandle.data());
       }
    }

    // No established dialog
    else if (foundEarlyDialog)
    {
       groupState = getGroupStateByCurrentHandle(earlyDialogHandle);
       if (groupState)
       {
          // Transition the dialog to established in *mpDialogMgr.
          sequence =
             mpDialogMgr->isNewRemoteTransaction(notifyRequest);

	  // Set dialogState to NULL so that a new SipSubscribeClient::SubscriptionDialogState
	  // state will be created below.
	  dialogState = Wrap<SubscriptionDialogState::Ptr>();

          Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                        "SipSubscribeClient::handleNotifyRequest Transition '%s' to '%s'",
                        earlyDialogHandle.data(), notifyDialogHandle.data());
       }
       else
       {
          sequence = SipDialogMgr::NO_DIALOG;
          Os::Logger::instance().log(FAC_SIP, PRI_WARNING,
                        "SipSubscribeClient::handleNotifyRequest No subscription group state found for handle '%s'",
                        earlyDialogHandle.data());
       }
    }
    else
    {
       // Neither an established nor an early dialog.
       // Check to see if this is from another fork of a subscription
       // that has already been established.
       UtlString establishedDialogHandle;
       if (mpDialogMgr->getEstablishedDialogHandleFor(earlyDialogHandle,
                                                      establishedDialogHandle))
       {
          // establishedDialogHandle is the existing subscription that
          // is a sibling fork of the current NOTIFY.
          Wrap<SubscriptionDialogState::Ptr> establishedState =
             getDialogState(establishedDialogHandle);
          if (establishedState)
          {
	     // Set dialogState to NULL so that a new SipSubscribeClient::SubscriptionDialogState
	     // state will be created below.
	     groupState = Wrap<SubscriptionGroupState::Ptr>(establishedState->mpGroupState);
	     dialogState = SubscriptionDialogState::Ptr();

             // Insert the subscription state into the Refresh Manager
             // (which will insert the dialog state into the Dialog
             // Manager).
             // Construct a SUBSCRIBE message for refreshing this
             // subscription by copying the SUBSCRIBE from the group state.
             // Note that the Contact from the NOTIFY will be edited
             // into the Dialog Manager's information when
             // SipDialogMgr::updateDialog() is called below.
             SipMessage* subscribeRequest =
                new SipMessage(*groupState->mpSubscriptionRequest);

             // Set the to-tag to match this NOTIFY's from-tag.
             Url fromUrl;
             notifyRequest.getFromUrl(fromUrl);
             UtlString fromTag;
             fromUrl.getFieldParameter("tag", fromTag);
             subscribeRequest->setToFieldTag(fromTag);
             UtlString handle;
             mpRefreshManager->initiateRefresh(subscribeRequest,
                                               this,
                                               SipSubscribeClient::refreshCallback,
                                               handle,
                                               // Do not send a SUBSCRIBE now.
                                               TRUE);
             // Set the expiration based on the expires value in the
             // NOTIFY, not the requested expiration in the SUBSCRIBE.
             if (expires != -1)
             {
                mpRefreshManager->changeCurrentRefreshTime(handle,
                                                           expires);
             }

             sequence = SipDialogMgr::IN_ORDER;

             // Adjust the flag variables to match what is now true.
             foundDialog = TRUE;

             Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                           "SipSubscribeClient::handleNotifyRequest Create dialog '%s', which is sibling to '%s'",
                           notifyDialogHandle.data(),
                           establishedDialogHandle.data());
          }
          else
          {
             sequence = SipDialogMgr::NO_DIALOG;
             Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                           "SipSubscribeClient::handleNotifyRequest No subscription dialog state found for handle '%s' which is sibling to NOTIFY '%s'",
                           establishedDialogHandle.data(),
                           notifyDialogHandle.data());
          }
       }
       else
       {
          sequence = SipDialogMgr::NO_DIALOG;
          Os::Logger::instance().log(FAC_SIP, PRI_ERR,
                        "SipSubscribeClient::handleNotifyRequest No subscription group state found for handle '%s'",
                        notifyDialogHandle.data());
       }
    }
    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "SipSubscribeClient::handleNotifyRequest "
                  "foundDialog = %d, foundEarlyDialog = %d, sequence = %d, ",
                  foundDialog, foundEarlyDialog, sequence);

    // If the request is OK but no dialog state has been created for it yet,
    // create it.
    if (!dialogState && sequence == SipDialogMgr::IN_ORDER)
    {
       // group State should have been set above.
       assert(groupState);

       // Create a subscription dialog based on this NOTIFY.
       SubscriptionDialogState::Ptr dialogStatePtr = SubscriptionDialogState::Ptr(
               new SubscriptionDialogState(notifyDialogHandle,
						 groupState.get(),
						 SUBSCRIPTION_SETUP));
       dialogState = Wrap<SubscriptionDialogState::Ptr>(dialogStatePtr);
       addDialogState(dialogStatePtr);

       if (groupState->mpStateCallback)
       {
            // Indicate that the subscription was established
            dialogState->mpGroupState->mpStateCallback(SUBSCRIPTION_SETUP,
                                 groupState->data(),
                                 dialogState->data(),
                                 groupState->mpApplicationData,
                                 -1, // no response code
                                 NULL, // no response text
                                 -1, // do not know expiration
                                 NULL); // no response
       }
       Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
		     "SipSubscribeClient::handleNotifyRequest Create dialog state for '%s'",
		     notifyDialogHandle.data());
    }

    // Construct the response based on the request's status.
    SipMessage subscriptionResponse;
    UtlBoolean bSendResponse = true;

    switch (sequence)
    {
    case SipDialogMgr::NO_DIALOG:
       // NOTIFY does not match any dialog.
       // If this NOTIFY is for termination, give a 200 response, to avoid
       // giving 481 responses to a NOTIFY confirming that we have just
       // terminated a subscription.
       // This allows a NOTIFY for a totally unknown subscription to get
       // a 200 response rather than a 481 response, but since the NOTIFY
       // claims the subscription is terminated, this won't prevent clearing
       // of dangling subscriptions.
    {
       if (terminated)
       {
          // Send special 200 response.
          subscriptionResponse.setOkResponseData(&notifyRequest);
       }
       else
       {
          // Send ordinary 481 response.
          subscriptionResponse.setBadSubscriptionData(&notifyRequest);
       }
    }
    break;

    case SipDialogMgr::IN_ORDER:
    {
       assert(dialogState);

       // Request is a new transaction (i.e. cseq greater than
       // last remote transaction)
       // Update the dialog
       mpDialogMgr->updateDialog(notifyRequest, notifyDialogHandle);

       // Note the creation of a dialog, if appropriate.
       if (!dialogState->mNotifyReceived)
       {
          dialogState->mpGroupState->mEstablishingNotifys++;
          dialogState->mNotifyReceived = true;
          Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                        "SipSubscribeClient::handleNotifyRequest "
                        "for group '%s' dialog '%s', incrementing mEstablishingNotifys to %d",
                        dialogState->mpGroupState->data(),
                        dialogState->data(),
                        dialogState->mpGroupState->mEstablishingNotifys);
       }

       // Invoke the Notify callback.
       if (dialogState->mState != SUBSCRIPTION_TERMINATED)
       {
          if (dialogState->mpGroupState->mpNotifyCallback)
          {
             bSendResponse = dialogState->mpGroupState->mpNotifyCallback(dialogState->mpGroupState->data(),
                                                         dialogState->data(),
                                                         dialogState->mpGroupState->mpApplicationData,
                                                         &notifyRequest);
          }
          if (bSendResponse)
          {
             // Send an OK response; this NOTIFY matched a subscription state.
             subscriptionResponse.setOkResponseData(&notifyRequest);
          }

          // If the NOTIFY says the state is terminated, end the subscription.
          if (terminated)
          {
	     Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
			   "SipSubscribeClient::handleNotifyRequest "
			   "ending subscription '%s' '%s' due to NOTIFY with "
			   "Subscription-State:terminated",
			   notifyDialogHandle.data(), dialogState->data());

             // Save the group state handle.
             UtlString originalHandle(dialogState->mpGroupState->data());

             // Delete knowledge of this subscription dialog, without sending
             // an un-SUBSCRIBE (which would be redundant).
             endSubscriptionDialogByNotifier(dialogState->data());


             dialogState = Wrap<SubscriptionDialogState::Ptr>();
             groupState = SubscriptionGroupState::Ptr();

             // Reestablish the subscription.
             reestablish(originalHandle);
          }
       }
       else
       {
          // Subscription has been previously terminated.
          Os::Logger::instance().log(FAC_SIP, PRI_WARNING,
                        "SipSubscribeClient::handleNotifyRequest "
                        "subscription for '%s' has been terminated",
                        notifyDialogHandle.data());

          // If this NOTIFY is for termination, give a 200 response, to avoid
          // giving 481 responses to a NOTIFY confirming that we have just
          // terminated a subscription.
          if (terminated)
          {
             // Send special 200 response.
             subscriptionResponse.setOkResponseData(&notifyRequest);
          }
          else
          {
             // Send ordinary 481 response.
             subscriptionResponse.setBadSubscriptionData(&notifyRequest);
          }
       }
    }
    break;

    case SipDialogMgr::LOOPED:
       subscriptionResponse.setInterfaceIpPort(notifyRequest.getInterfaceIp(),
                                               notifyRequest.getInterfacePort());
       subscriptionResponse.setResponseData(&notifyRequest,
                                            SIP_LOOP_DETECTED_CODE,
                                            SIP_LOOP_DETECTED_TEXT);
       /* Set "Retry-After: 0" to prevent termination of the subscription.
        * The reason we don't want the notifier to terminate the
        * subscription is that we must have received and processed
        * another copy of this NOTIFY, so there is no failure from
        * our point of view.
        * See RFC 3265, section 3.2.2 for why "Retry-After: 0" has
        * this effect.
        */
       subscriptionResponse.setHeaderValue(SIP_RETRY_AFTER_FIELD, "0");
       break;

    case SipDialogMgr::OUT_OF_ORDER:
       subscriptionResponse.setInterfaceIpPort(notifyRequest.getInterfaceIp(),
                                               notifyRequest.getInterfacePort());
       subscriptionResponse.setResponseData(&notifyRequest,
                                            SIP_OUT_OF_ORDER_CODE,
                                            SIP_OUT_OF_ORDER_TEXT);
       /* Try to prevent the notifier from terminating the subscription,
        * since we have received a later version of the information.
        * If having all the NOTIFYs is important, the application
        * code will detect and act on the missing "version".
        */
       subscriptionResponse.setHeaderValue(SIP_RETRY_AFTER_FIELD, "0");
       break;
    }

    if (bSendResponse)
    {
       // Send the response.
       mpUserAgent->send(subscriptionResponse);
    }
}

void SipSubscribeClient::addGroupState(SubscriptionGroupState::Ptr &groupState)
{
    OsLock lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "SipSubscribeClient::addGroupState groupState = %p '%s' size=%d",
                  groupState.get(),
                  groupState->data(),
                  mSubscriptionGroups.size());

    mSubscriptionGroups.push_back(groupState);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::addGroupState mSubscriptionGroups p=%p s=%s c=%d size=%d",
                  groupState.get(), groupState->mCurrentEarlyDialogHandle.data(), groupState.use_count(), mSubscriptionGroups.size());

    std::pair<std::map<UtlString,SubscriptionGroupState::Ptr>::iterator,bool> ret;

    ret = mSubscriptionGroupsByCurrentHandle.insert(
            //std::pair<UtlString,SubscriptionGroupState::Ptr>(
                    std::make_pair(groupState->mCurrentEarlyDialogHandle, groupState));

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::addGroupState mSubscriptionGroupsByCurrentHandle p=%p s=%s c=%d size=%d",
                  groupState.get(), groupState->mCurrentEarlyDialogHandle.data(), groupState.use_count(), mSubscriptionGroupsByCurrentHandle.size());

    /* TODO: The return should be properly checked to make sure the map insert succeeded.
     * (avoid duplicates as this assert is not enough)*/
    assert(ret.second);
}

void SipSubscribeClient::addDialogState(SubscriptionDialogState::Ptr &dialogState)
{
    OsLock lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "SipSubscribeClient::addDialogState dialogState = %p '%s' size=%d",
                  dialogState.get(),
                  dialogState->data(),
                  mSubscriptionDialogs.size());

    mSubscriptionDialogs.push_back(dialogState);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::addDialogState mSubscriptionDialogs p=%p s=%s c=%d size=%d",
                  dialogState.get(), dialogState->data(), dialogState.use_count(), mSubscriptionDialogs.size());

}

/**
 * @brief Helper function used to search in a map with elements of type
 * std::pair<UtlString,SipSubscribeClient::SubscriptionGroupState::Ptr>
 */
bool compareGroupStateByCurrentHandle(std::pair<UtlString,SipSubscribeClient::SubscriptionGroupState::Ptr> value, const UtlString& dialogHandle)
{
    return value.first.data() == dialogHandle;
}

/**
 * @brief Helper function used to search in a vector with elements of type
 * SubscriptionGroupState::Ptr
 */
bool compareGroupStateByHandle(SipSubscribeClient::SubscriptionGroupState::Ptr groupState, const UtlString& dialogHandle)
{
    if (groupState)
    {
        return (groupState.get()->data() == dialogHandle);
    }

    return false;
}

/**
 * @brief Helper function used to search in a vector with elements of type
 * SubscriptionDialogState::Ptr
 */
bool compareDialogStateByHandle(SipSubscribeClient::SubscriptionDialogState::Ptr dialogState, const UtlString& dialogHandle)
{
    if (dialogState)
    {
        return (dialogState->data() == dialogHandle);
    }

    return false;
}

bool compareDialogStateByGroupState(SipSubscribeClient::SubscriptionDialogState::Ptr dialogState, const SipSubscribeClient::SubscriptionGroupState::Ptr &groupState)
{
    if (dialogState && groupState)
    {
        return (dialogState->mpGroupState.get() == groupState.get());
    }

    return false;
}

Wrap<SipSubscribeClient::SubscriptionGroupState::Ptr> SipSubscribeClient::getGroupStateByOriginalHandle(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    SubscriptionGroupState::Ptr foundState;

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::getGroupStateByOriginalHandle dialogHandle s=%s",
                  dialogHandle.data());

    std::vector<SubscriptionGroupState::Ptr>::iterator iterator =
             std::find_if(mSubscriptionGroups.begin(), mSubscriptionGroups.end(), boost::bind(compareGroupStateByHandle, _1, dialogHandle));

    if (iterator != mSubscriptionGroups.end())
    {
            foundState = *iterator;
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::getGroupStateByOriginalHandle FOUND in mSubscriptionGroups p=%p s=%s c=%d size=%d",
                          foundState.get(), foundState->mCurrentEarlyDialogHandle.data(), foundState.use_count(), mSubscriptionGroups.size());
    }
    else
    {
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::getGroupStateByOriginalHandle NOT FOUND in mSubscriptionGroups");
    }

    if (foundState)
    {
        assert( mSubscriptionGroupsByCurrentHandle.end() !=
                 std::find_if(mSubscriptionGroupsByCurrentHandle.begin(),
                         mSubscriptionGroupsByCurrentHandle.end(),
                         boost::bind(compareGroupStateByCurrentHandle, _1, foundState->mCurrentEarlyDialogHandle)));

        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::getGroupStateByOriginalHandle FOUND TOO in mSubscriptionGroupsByCurrentHandle");
     }

    OsUnLock unlock(lock);

    return Wrap<SubscriptionGroupState::Ptr>(foundState);
}

Wrap<SipSubscribeClient::SubscriptionGroupState::Ptr> SipSubscribeClient::getGroupStateByCurrentHandle(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::getGroupStateByCurrentHandle dialogHandle s=%s",
                  dialogHandle.data());

    SubscriptionGroupState::Ptr foundState;
    std::map<UtlString, SubscriptionGroupState::Ptr>::iterator iterator =
    std::find_if(mSubscriptionGroupsByCurrentHandle.begin(),
            mSubscriptionGroupsByCurrentHandle.end(),
            boost::bind(compareGroupStateByCurrentHandle, _1, dialogHandle));

    if (iterator != mSubscriptionGroupsByCurrentHandle.end())
    {
        foundState = iterator->second;
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::getGroupStateByCurrentHandle FOUND in mSubscriptionGroupsByCurrentHandle p=%p s=%s c=%d  size=%d",
                      foundState.get(), foundState->mCurrentEarlyDialogHandle.data(), foundState.use_count(), mSubscriptionGroupsByCurrentHandle.size());
    }
    else
    {
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::getGroupStateByCurrentHandle NOT FOUND in mSubscriptionGroupsByCurrentHandle");
    }

    if (foundState)
    {
        assert(mSubscriptionGroups.end() != std::find(mSubscriptionGroups.begin(), mSubscriptionGroups.end(), foundState));
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::getGroupStateByCurrentHandle FOUND TOO in mSubscriptionGroups");
    }

    OsUnLock unlock(lock);

    return Wrap<SubscriptionGroupState::Ptr>(foundState);
}

Wrap<SipSubscribeClient::SubscriptionDialogState::Ptr> SipSubscribeClient::getDialogState(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    SubscriptionDialogState::Ptr foundState;

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::getDialogState dialogHandle s=%s",
                  dialogHandle.data());


    std::vector<SubscriptionDialogState::Ptr>::iterator iterator =
             std::find_if(mSubscriptionDialogs.begin(), mSubscriptionDialogs.end(), boost::bind(compareDialogStateByHandle, _1, dialogHandle));

    if (iterator != mSubscriptionDialogs.end())
    {
            foundState = *iterator;

            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::getDialogState FOUND in mSubscriptionDialogs p=%p s=%s c=%d  size=%d",
                          foundState.get(), foundState->data(), foundState.use_count(), mSubscriptionDialogs.size());

    }
    else
    {
        // Swap the tags around to see if it is keyed the other way
        UtlString reversedHandle;
        SipDialog::reverseTags(dialogHandle, reversedHandle);

        iterator = std::find_if(mSubscriptionDialogs.begin(), mSubscriptionDialogs.end(), boost::bind(compareDialogStateByHandle, _1, reversedHandle));
        if (iterator != mSubscriptionDialogs.end())
        {
                foundState = *iterator;
                Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                              "QQQ SipSubscribeClient::getDialogState FOUND in mSubscriptionDialogs p=%p s=%s c=%d  size=%d",
                              foundState.get(), foundState->data(), foundState.use_count(), mSubscriptionDialogs.size());
        }
        else
        {
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::getDialogState NOT FOUND in mSubscriptionDialogs");
        }
    }

    OsUnLock unlock(lock);

    return Wrap<SubscriptionDialogState::Ptr>(foundState);
}

Wrap<SipSubscribeClient::SubscriptionDialogState::Ptr> SipSubscribeClient::getDialogStateByGroupState(const SubscriptionGroupState::Ptr& groupState)
{
    OsLockUnlockable lock(mSemaphore);

    SubscriptionDialogState::Ptr foundState;

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::getDialogStateByGroupState groupState p=%p",
                  groupState.get());


    std::vector<SubscriptionDialogState::Ptr>::iterator iterator =
             std::find_if(mSubscriptionDialogs.begin(), mSubscriptionDialogs.end(), boost::bind(compareDialogStateByGroupState, _1, groupState));

    if (iterator != mSubscriptionDialogs.end())
    {
            foundState = *iterator;

            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::getDialogState FOUND in mSubscriptionDialogs p=%p s=%s c=%d  size=%d",
                          foundState.get(), foundState->data(), foundState.use_count(), mSubscriptionDialogs.size());

    }

    OsUnLock unlock(lock);

    return Wrap<SubscriptionDialogState::Ptr>(foundState);
}


void SipSubscribeClient::reindexGroupState(SubscriptionGroupState::Ptr groupState)
{
    OsLock lock(mSemaphore);

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "QQQ SipSubscribeClient::reindexGroupState original handle = '%s', previous handle = '%s' size=%d",
                 groupState->data(),
                 groupState->mCurrentEarlyDialogHandle.data(),
                 mSubscriptionGroupsByCurrentHandle.size());

   // Remove the entry in mSubscriptionGroupsByCurrentHandle.
   std::map<UtlString, SubscriptionGroupState::Ptr>::iterator map_iterator =
           std::find_if(mSubscriptionGroupsByCurrentHandle.begin(),
                   mSubscriptionGroupsByCurrentHandle.end(),
                   boost::bind(compareGroupStateByCurrentHandle, _1, groupState->mCurrentEarlyDialogHandle));

   if (map_iterator != mSubscriptionGroupsByCurrentHandle.end())
   {
       mSubscriptionGroupsByCurrentHandle.erase(map_iterator);
       Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                     "QQQ SipSubscribeClient::reindexGroupState ERASED in mSubscriptionGroupsByCurrentHandle");
   }
   else
   {
       assert(false);
       Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                     "QQQ SipSubscribeClient::reindexGroupState NOT FOUND in mSubscriptionGroupsByCurrentHandle");
   }

   // Calculate the new early dialog handle and record it in mCurrentEarlyDialogHandle.
   groupState->mpSubscriptionRequest->getDialogHandle(groupState->mCurrentEarlyDialogHandle);

   // Correct the entry in mSubscriptionGroupsByCurrentHandle.
   std::pair<std::map<UtlString,SubscriptionGroupState::Ptr>::iterator,bool> ret;

   ret = mSubscriptionGroupsByCurrentHandle.insert(
           std::pair<UtlString,SubscriptionGroupState::Ptr>(
                   groupState->mCurrentEarlyDialogHandle,groupState));

   /* TODO: The return should be properly checked to avoid duplicates as this assert is not enough*/
   assert(ret.second);

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::reindexGroupState new handle = '%s' size=%d",
                 groupState->mCurrentEarlyDialogHandle.data(),
                 mSubscriptionGroupsByCurrentHandle.size());
}

Wrap<SipSubscribeClient::SubscriptionGroupState::Ptr> SipSubscribeClient::removeGroupStateByCurrentHandle(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::removeGroupStateByCurrentHandle dialogHandle s=%s  size=%d %d",
                  dialogHandle.data(),
                  mSubscriptionGroups.size(),
                  mSubscriptionGroupsByCurrentHandle.size());

    SubscriptionGroupState::Ptr foundState;
    std::map<UtlString, SubscriptionGroupState::Ptr>::iterator remove_it =
            std::find_if(mSubscriptionGroupsByCurrentHandle.begin(),
                    mSubscriptionGroupsByCurrentHandle.end(),
                    boost::bind(compareGroupStateByCurrentHandle, _1, dialogHandle));

    if (remove_it != mSubscriptionGroupsByCurrentHandle.end())
    {
        foundState = remove_it->second;
        mSubscriptionGroupsByCurrentHandle.erase(remove_it);
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::removeGroupStateByCurrentHandle ERASED in mSubscriptionGroupsByCurrentHandle");
    }
    else
    {
        Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                      "QQQ SipSubscribeClient::removeGroupStateByCurrentHandle NOT FOUND in mSubscriptionGroupsByCurrentHandle");
    }

    if (foundState)
    {
        std::vector<SubscriptionGroupState::Ptr>::iterator vec_iterator =
                std::find(mSubscriptionGroups.begin(), mSubscriptionGroups.end(), foundState);
        if (vec_iterator != mSubscriptionGroups.end())
        {
            mSubscriptionGroups.erase(vec_iterator);
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::removeGroupStateByCurrentHandle ERASED in mSubscriptionGroups");
        }
        else
        {
            assert(false);
            Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                          "QQQ SipSubscribeClient::removeGroupStateByCurrentHandle NOT FOUND in mSubscriptionGroups");
        }
    }

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::removeGroupStateByCurrentHandle dialogHandle = '%s', foundState = %p '%s'  size=%d %d",
                 dialogHandle.data(),
                 (foundState) ? foundState.get() : NULL,
                 (foundState) ? foundState->data() : "(null)",
                 mSubscriptionGroups.size(),
                 mSubscriptionGroupsByCurrentHandle.size());

   OsUnLock unlock(lock);

   return Wrap<SubscriptionGroupState::Ptr>(foundState);
}

Wrap<SipSubscribeClient::SubscriptionGroupState::Ptr> SipSubscribeClient::removeGroupStateByOriginalHandle(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::removeGroupStateByOriginalHandle dialogHandle s=%s  size=%d %d",
                  dialogHandle.data(),
                  mSubscriptionGroups.size(),
                  mSubscriptionGroupsByCurrentHandle.size());


    SubscriptionGroupState::Ptr foundState;
    std::vector<SubscriptionGroupState::Ptr>::iterator iterator =
      std::find_if(mSubscriptionGroups.begin(), mSubscriptionGroups.end(), boost::bind(compareGroupStateByHandle, _1, dialogHandle));

   if (iterator != mSubscriptionGroups.end())
   {
       foundState = *iterator;
       mSubscriptionGroups.erase(iterator);
       Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                     "QQQ SipSubscribeClient::removeGroupStateByOriginalHandle ERASED in mSubscriptionGroups");



       std::map<UtlString, SubscriptionGroupState::Ptr>::iterator remove_it =
               std::find_if(mSubscriptionGroupsByCurrentHandle.begin(),
                       mSubscriptionGroupsByCurrentHandle.end(),
                       boost::bind(compareGroupStateByCurrentHandle, _1, foundState->mCurrentEarlyDialogHandle));
       if (remove_it != mSubscriptionGroupsByCurrentHandle.end())
       {
           mSubscriptionGroupsByCurrentHandle.erase(remove_it);
           Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                         "QQQ SipSubscribeClient::removeGroupStateByOriginalHandle ERASED in mSubscriptionGroupsByCurrentHandle");
       }
       else
       {
           assert(false);
       }
   }

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::removeGroupStateByOriginalHandle dialogHandle = '%s', foundState = %p '%s'  size=%d %d",
                 dialogHandle.data(),
                 (foundState) ? foundState.get() : NULL,
                 (foundState) ? foundState->data() : "(null)",
                 mSubscriptionGroups.size(),
                 mSubscriptionGroupsByCurrentHandle.size());

   OsUnLock unlock(lock);

   return Wrap<SubscriptionGroupState::Ptr>(foundState);
}

Wrap<SipSubscribeClient::SubscriptionDialogState::Ptr> SipSubscribeClient::removeDialogState(const UtlString& dialogHandle)
{
    OsLockUnlockable lock(mSemaphore);

    Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                  "QQQ SipSubscribeClient::removeDialogState dialogHandle s=%s  size=%d %d",
                  dialogHandle.data(),
                  mSubscriptionDialogs.size());

    SubscriptionDialogState::Ptr foundState;

    std::vector<SubscriptionDialogState::Ptr>::iterator iterator =
             std::find_if(mSubscriptionDialogs.begin(), mSubscriptionDialogs.end(), boost::bind(compareDialogStateByHandle, _1, dialogHandle));

    if (iterator != mSubscriptionDialogs.end())
    {
            foundState = *iterator;
            mSubscriptionDialogs.erase(iterator);
    }
    else
    {
        // Swap the tags around to see if it is keyed the other way
        UtlString reversedHandle;
        SipDialog::reverseTags(dialogHandle, reversedHandle);

        iterator = std::find_if(mSubscriptionDialogs.begin(), mSubscriptionDialogs.end(), boost::bind(compareDialogStateByHandle, _1, reversedHandle));
        if (iterator != mSubscriptionDialogs.end())
        {
                foundState = *iterator;
                mSubscriptionDialogs.erase(iterator);
        }
    }

   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::removeDialogState dialogHandle = '%s', foundState = %p '%s'",
                 dialogHandle.data(),
                 (foundState) ? foundState.get() : NULL,
                 (foundState) ? foundState->data() : "(null)");

   OsUnLock unlock(lock);

   return Wrap<SubscriptionDialogState::Ptr>(foundState);
}

/* ============================ FUNCTIONS ================================= */

// The following functions implement the strategy for restarting subscriptions
// that fail while attempting to start them, or fail after they are established.
// At subscription starting, or after a subscription dialog fails,
// a new SUBSCRIBE (with a new Call-Id) is sent.
// We wait for 15 seconds.  After 15 seconds, we check to see if the starting
// attempt was successful:
//        at least one success response to the SUBSCRIBE
//        no failure response to the SUBSCRIBE
//        at least one NOTIFY arrived (that does not have
//           "Subscription-State: terminated")
// If that attempt fails, we cancel the dialogs created by that SUBSCRIBE,
// and try again with a doubled waiting time.
// Attempts are repeated, doubling the waiting time each time, until
// the waiting time exceeds 5 minutes, at which time cycles continue
// indefinitely without increasing the waiting time.

// Has the subscription start succeeded?
bool SipSubscribeClient::SubscriptionGroupState::successfulStart()
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::successfulStart "
                 "for group '%s', mSuccessResponses = %d, mFailureResponses = %d, mEstablishingNotifys = %d",
                 data(),
                 mSuccessResponses, mFailureResponses, mEstablishingNotifys);
   return
      mSuccessResponses > 0 &&
      mFailureResponses == 0 &&
      mEstablishingNotifys > 0;
}

// Set up the starting timer.
void SipSubscribeClient::SubscriptionGroupState::setStartingTimer(bool initial)
{
   // Use the initial starting time (if initial == true (when called
   // from ::addSubscription()) or !mStarting (failure of an
   // established subscription).
   // Otherwise, double the previously-used starting time, if that was
   // less than SUBSCRIPTION_STARTUP_MAX.
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::setStartingTimer "
                 "for group '%s', initial = %d, mStarting = %d, mStartingTimeout = %d",
                 data(),
                 initial, mStarting, mStartingTimeout);
   mStartingTimeout =
      initial || !mStarting ? SUBSCRIPTION_STARTUP_INITIAL :
      mStartingTimeout <= SUBSCRIPTION_STARTUP_MAX ? mStartingTimeout * 2 :
      mStartingTimeout;
   mStarting = true;            // Set mStarting after using it above.
   mSuccessResponses = 0;
   mFailureResponses = 0;
   mEstablishingNotifys = 0;
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::setStartingTimer "
                 "setting mStarting = %d, mSuccessResponses = %d, mFailureResponses = %d, mEstablishingNotifys = %d",
                 mStarting, mSuccessResponses, mFailureResponses, mEstablishingNotifys);

   // We stop mStartingTimer asynchronously here because if it has a queued
   // firing, that will do no harm:  This starting will fail prematurely
   // and another start will be done.  This code does not delete the
   // SubscriptionGroupState.
   mStartingTimer.stop(FALSE); // async
   mStartingTimer.oneshotAfter(OsTime(mStartingTimeout, 0));
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::setStartingTimer "
                 "for group '%s', setting mStartingTimer to fire in %d sec",
                 data(), mStartingTimeout);
}

// Set the subscription group to established state.
void SipSubscribeClient::SubscriptionGroupState::transitionToEstablished()
{
   mStarting = false;
}

// Prepare for the next starting attempt.
void SipSubscribeClient::SubscriptionGroupState::resetStarting()
{
   // Set a new Call-Id into the SUBSCRIBE request.
   UtlString callId;
   CallId::getNewCallId(callId);
   mpSubscriptionRequest->setCallIdField(callId);

   // Set a new from tag.
   Url fromUrl;
   mpSubscriptionRequest->getFromUrl(fromUrl);
   UtlString fromFieldValue;
   fromUrl.toString(fromFieldValue);
   UtlString fromTag;
   CallId::getNewTag(fromTag);
   fromUrl.setFieldParameter("tag", fromTag);
   fromUrl.toString(fromFieldValue);
   mpSubscriptionRequest->setRawFromField(fromFieldValue);
}

void SipSubscribeClient::reestablish(const UtlString& handle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::reestablish handle = '%s'",
                 handle.data());

   // Terminate all current subscriptions for this group.
   // (This should be factored with ::endSubscriptionGroup.)

   OsLockUnlockable lock(mSemaphore);

   // Repeatedly look up the group and terminate one subscription.
   {
      int count = 0;
      UtlBoolean found;

      do {
         found = FALSE;
         // Look up the group state.
         // We must find the group state again on every iteration because
         // we unlock mSemaphore before calling ::endSubscriptionDialog() below.
         Wrap<SubscriptionGroupState::Ptr> groupState =
            getGroupStateByOriginalHandle(handle);
         // Check that reestablishment is set for this group.
         if (groupState && groupState->mReestablish)
         {
            std::vector<SubscriptionDialogState::Ptr>::iterator iterator = mSubscriptionDialogs.begin();

            found = FALSE;
            while (!found &&
                    iterator != mSubscriptionDialogs.end())
            {
               found = (*iterator)->mpGroupState.get() == groupState.get().get();
               if (found)
               {
                  // Remember the key of the dialog.
                  UtlString key((*iterator)->data());
                  count++;
                  // Unlock mSemaphore so ::endSubscriptionDialog() can lock it.
                  OsUnLock unlock(lock);
                  groupState = Wrap<SubscriptionGroupState::Ptr>();

                  endSubscriptionDialog(key);
               }
            }
         }
      } while (found);

      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::reestablish ended %d dialogs",
	            count);

      // Look up the group state.
      // We must find the group state again because we unlocked
      // mSemaphore before calling ::endSubscriptionDialog() above.
      Wrap<SubscriptionGroupState::Ptr> groupState =
         getGroupStateByOriginalHandle(handle);

      // Test whether reestablish is set.
      // It may be false because the subscription group is being ended.
      if (groupState && groupState->mReestablish)
      {
         // Prepare for the next starting attempt.
         groupState->resetStarting();
         // Reindex the group under the new early dialog handle.
         reindexGroupState(groupState.get());

         // Try establishing the subscription again.
         groupState->setStartingTimer(false);

         // Give a copy of the request to the refresh manager to send the
         // SUBSCRIBE and keep the subscription alive.
         UtlString earlyDialogHandle;

         mpRefreshManager->
            initiateRefresh(new SipMessage(*groupState->mpSubscriptionRequest),
                            //< give ownership to mpRefreshManager
                            this,
                            // refreshCallback receives the SipSubscribeClient as app. data
                            SipSubscribeClient::refreshCallback,
                            earlyDialogHandle);
      }
   }
}

// The timer message processing routine for SipSubscribeClient::mStartingTimer.
OsStatus SipSubscribeClient::handleStartingEvent(const UtlString& handle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SubscriptionStartingNotification::signal Timer fired, groupState handle = '%s'",
                 handle.data());

   // Get the SubscriptionGroupState.
   // Due to race conditions, it may no longer exist.
   Wrap<SubscriptionGroupState::Ptr> groupState = getGroupStateByOriginalHandle(handle);
   if (groupState)
   {
      // Check to see if we set up the subscription(s) successfully.
      if (groupState->successfulStart())
      {
         // If so, update the state variables.
         groupState->transitionToEstablished();
      }
      else
      {
         // Test whether reestablish is set.
         // It may be false because the subscription is not supposed to restart,
         // or because the subscription group is being ended.
         if (groupState->mReestablish)
         {
            groupState = SubscriptionGroupState::Ptr();

            reestablish(handle);
         }
      }
   }
   else
   {
      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::handleStartingEvent no SubscriptionGroupState found");
   }

   return OS_SUCCESS;
}

// The following functions implement the feature of restarting subscriptions
// at long intervals (12 to 24 hours), to revise them if the routing of
// the notifier URI changes.

// Random number generator.
UtlRandom SipSubscribeClient::SubscriptionGroupState::sRandom;

// Set the restart timer.
void SipSubscribeClient::SubscriptionGroupState::setRestartTimer()
{
   // Choose a random time between 1/2 and 1 times RESTART_INTERVAL.
   int refresh_time =
      (int) ((1.0 + ((float) sRandom.rand()) / RAND_MAX) / 2.0 *
             RESTART_INTERVAL);
   Os::Logger::instance().log(FAC_RLS, PRI_DEBUG,
                 "SubscriptionGroupState::setRestartTimer for group '%s', refresh_time = %d",
                 data(), refresh_time);
   OsTime rt(refresh_time, 0);
   mRestartTimer.oneshotAfter(rt);
}

// The timer message processing routine for SipSubscribeClient::mRestartTimer.
OsStatus SipSubscribeClient::handleRestartEvent(const UtlString& handle)
{
   Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                 "SipSubscribeClient::handleRestartEvent Timer fired, groupState handle = '%s'",
                 handle.data());

   // Get the SubscriptionGroupState.
   // Due to race conditions, it may no longer exist.
   Wrap<SubscriptionGroupState::Ptr> groupState = getGroupStateByOriginalHandle(handle);
   if (groupState)
   {
      // Test whether restart is set.
      // It may be false because the subscription group is being ended.
      if (groupState->mRestart)
      {
         groupState->setRestartTimer();

         groupState = SubscriptionGroupState::Ptr();
         
         reestablish(handle);
      }
   }
   else
   {
      Os::Logger::instance().log(FAC_SIP, PRI_DEBUG,
                    "SipSubscribeClient::handleRestartEvent no SubscriptionGroupState found");
   }

   return OS_SUCCESS;
}
