/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

void MACAddress::findAllAddresses (Array<MACAddress>& result)
{
    ifaddrs* addrs = nullptr;

    if (getifaddrs (&addrs) == 0)
    {
        for (const ifaddrs* cursor = addrs; cursor != nullptr; cursor = cursor->ifa_next)
        {
            // Required to avoid misaligned pointer access
            sockaddr sto;
            std::memcpy (&sto, cursor->ifa_addr, sizeof (sockaddr));

            if (sto.sa_family == AF_LINK)
            {
                auto sadd = reinterpret_cast<const sockaddr_dl*> (cursor->ifa_addr);

               #ifndef IFT_ETHER
                enum { IFT_ETHER = 6 };
               #endif

                if (sadd->sdl_type == IFT_ETHER)
                {
                    MACAddress ma (((const uint8*) sadd->sdl_data) + sadd->sdl_nlen);

                    if (! ma.isNull())
                        result.addIfNotAlreadyThere (ma);
                }
            }
        }

        freeifaddrs (addrs);
    }
}

//==============================================================================
bool JUCE_CALLTYPE Process::openEmailWithAttachments ([[maybe_unused]] const String& targetEmailAddress,
                                                      [[maybe_unused]] const String& emailSubject,
                                                      [[maybe_unused]] const String& bodyText,
                                                      [[maybe_unused]] const StringArray& filesToAttach)
{
  #if JUCE_IOS
    //xxx probably need to use MFMailComposeViewController
    jassertfalse;
    return false;
  #else
    JUCE_AUTORELEASEPOOL
    {
        String script;
        script << "tell application \"Mail\"\r\n"
                  "set newMessage to make new outgoing message with properties {subject:\""
               << emailSubject.replace ("\"", "\\\"")
               << "\", content:\""
               << bodyText.replace ("\"", "\\\"")
               << "\" & return & return}\r\n"
                  "tell newMessage\r\n"
                  "set visible to true\r\n"
                  "set sender to \"sdfsdfsdfewf\"\r\n"
                  "make new to recipient at end of to recipients with properties {address:\""
               << targetEmailAddress
               << "\"}\r\n";

        for (int i = 0; i < filesToAttach.size(); ++i)
        {
            script << "tell content\r\n"
                      "make new attachment with properties {file name:\""
                   << filesToAttach[i].replace ("\"", "\\\"")
                   << "\"} at after the last paragraph\r\n"
                      "end tell\r\n";
        }

        script << "end tell\r\n"
                  "end tell\r\n";

        NSAppleScript* s = [[NSAppleScript alloc] initWithSource: juceStringToNS (script)];
        NSDictionary* error = nil;
        const bool ok = [s executeAndReturnError: &error] != nil;
        [s release];

        return ok;
    }
  #endif
}

namespace detail
{
enum class WebInputStreamDiagnosticEventType : std::uint8_t
{
    taskRegistered,
    responseBodyWait,
    taskRetired,
    tokenReleased
};

struct WebInputStreamDiagnosticEvent
{
    WebInputStreamDiagnosticEventType type {};
    std::uint64_t generation = 0;
    std::uint64_t taskIdentifier = 0;
    std::int64_t requestUrlHash = 0;
    std::int64_t bytesConsumedByRead = 0;
    std::uint64_t ownerId = 0;
};

struct WebInputStreamDiagnosticSnapshot
{
    std::uint64_t ownerId = 0;
    std::uint64_t ownerConstructionCount = 0;
    std::uint64_t acceptingGenerationId = 0;
    std::uint64_t totalGenerationConstructions = 0;
    std::uint64_t liveGenerationCount = 0;
    std::uint64_t liveListenerCount = 0;
    std::uint64_t activeCallbackCount = 0;
    std::uint64_t inFlightDiagnosticLifecyclePublicationCount = 0;
    std::uint64_t liveTokenLeaseCount = 0;
    std::uint64_t totalRegisteredTaskCount = 0;
    std::uint64_t totalRetiredTaskCount = 0;
    std::uint64_t totalReleasedTokenCount = 0;
};

/** Internal, allocation-free lifecycle observation. The sink may run while a
    URLConnectionState mutex is held, so it must be non-blocking. The setter
    only exchanges the sink; callers must quiesce observed requests before
    destroying the sink's storage. */
using WebInputStreamDiagnosticSink = void (*) (WebInputStreamDiagnosticEvent) noexcept;

namespace
{
std::atomic<WebInputStreamDiagnosticSink>& getWebInputStreamDiagnosticSink() noexcept
{
    static std::atomic<WebInputStreamDiagnosticSink> sink { nullptr };
    return sink;
}
}

WebInputStreamDiagnosticSink setWebInputStreamDiagnosticSink (WebInputStreamDiagnosticSink sink) noexcept
{
    return getWebInputStreamDiagnosticSink().exchange (sink, std::memory_order_acq_rel);
}

static bool hasWebInputStreamDiagnosticSink() noexcept
{
    return getWebInputStreamDiagnosticSink().load (std::memory_order_acquire) != nullptr;
}

static std::int64_t getWebInputStreamDiagnosticRequestUrlHash (NSURLRequest* request)
{
    if (! hasWebInputStreamDiagnosticSink() || request == nullptr || [request URL] == nil)
        return 0;

    return nsStringToJuce ([[request URL] absoluteString]).hashCode64();
}

static void publishWebInputStreamDiagnosticEvent (WebInputStreamDiagnosticEvent event) noexcept
{
    if (const auto sink = getWebInputStreamDiagnosticSink().load (std::memory_order_acquire))
        sink (event);
}

WebInputStreamDiagnosticSnapshot getWebInputStreamDiagnosticSnapshot();
WebInputStreamDiagnosticSnapshot waitForWebInputStreamDiagnosticQuiescence (std::chrono::milliseconds timeout);
} // namespace detail

namespace
{
std::atomic<std::uint64_t> nextWebInputStreamDiagnosticOwnerId { 1 };
std::atomic<std::uint64_t> webInputStreamDiagnosticOwnerConstructionCount { 0 };
}

//==============================================================================
struct SessionListener
{
    virtual ~SessionListener() = default;
    virtual void didComplete (NSError*) = 0;
    virtual void didReceiveResponse (NSURLResponse*, void (^) (NSURLSessionResponseDisposition)) = 0;
    virtual void didReceiveData (NSData*) = 0;
    virtual void didSendBodyData (int64_t) = 0;
    virtual void willPerformHTTPRedirection (NSURLRequest*, void (^) (NSURLRequest *)) = 0;
};

struct RegisteredSessionTask
{
    NSUniquePtr<NSURLSessionTask> task;
    std::uint64_t generation = 0;
    NSUInteger identifier = 0;
    std::int64_t requestUrlHash = 0;

    explicit operator bool() const noexcept { return task != nullptr; }
};

class SharedSession
{
    enum class Lifecycle
    {
        running,
        shuttingDown
    };

    struct ListenerRecord
    {
        SessionListener* listener = nullptr;
        size_t activeCallbacks = 0;
        bool terminalCallbackStarted = false;
        std::int64_t requestUrlHash = 0;
    };

    struct SessionGeneration
    {
        SessionGeneration (std::uint64_t generationToUse,
                           NSObject<NSURLSessionTaskDelegate>* delegateToUse)
            : generation (generationToUse),
              session ([[NSURLSession sessionWithConfiguration: [NSURLSessionConfiguration defaultSessionConfiguration]
                                                       delegate: delegateToUse
                                                  delegateQueue: nil] retain])
        {
        }

        std::uint64_t generation = 0;
        NSUniquePtr<NSURLSession> session;
        std::map<NSUInteger, ListenerRecord> listeners;
        bool invalidationRequested = false;
        bool invalidationCallbackReceived = false;
    };

    void releaseListenerCallback (std::uint64_t generation,
                                  NSUInteger identifier) noexcept;

    class ListenerCallback
    {
    public:
        ListenerCallback() = default;

        ListenerCallback (SharedSession* ownerToUse,
                          std::uint64_t generationToUse,
                          NSUInteger identifierToUse,
                          SessionListener* listenerToUse) noexcept
            : owner (ownerToUse),
              generation (generationToUse),
              identifier (identifierToUse),
              listener (listenerToUse)
        {
        }

        ListenerCallback (ListenerCallback&& other) noexcept
            : owner (std::exchange (other.owner, nullptr)),
              generation (other.generation),
              identifier (other.identifier),
              listener (other.listener)
        {
        }

        ListenerCallback& operator= (ListenerCallback&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                owner = std::exchange (other.owner, nullptr);
                generation = other.generation;
                identifier = other.identifier;
                listener = other.listener;
            }

            return *this;
        }

        ~ListenerCallback();

        explicit operator bool() const noexcept { return listener != nullptr; }
        SessionListener* operator->() const noexcept { return listener; }

    private:
        void reset() noexcept;

        SharedSession* owner = nullptr;
        std::uint64_t generation = 0;
        NSUInteger identifier = 0;
        SessionListener* listener = nullptr;
    };

    class ScopedCallback
    {
    public:
        explicit ScopedCallback (SharedSession& ownerToUse)
            : owner (ownerToUse)
        {
            const std::scoped_lock lock { owner.mutex };
            ++owner.activeCallbackCount;
        }

        ~ScopedCallback()
        {
            const std::scoped_lock lock { owner.mutex };
            jassert (owner.activeCallbackCount > 0);
            --owner.activeCallbackCount;
            owner.condvar.notify_all();
        }

    private:
        SharedSession& owner;
    };

public:
    SharedSession()
        : ownerId (nextWebInputStreamDiagnosticOwnerId.fetch_add (1, std::memory_order_relaxed))
    {
        jassert (ownerId > 0);
        webInputStreamDiagnosticOwnerConstructionCount.fetch_add (1, std::memory_order_relaxed);
        DelegateClass::setState (delegate.get(), this);
    }

    std::uint64_t getOwnerId() const noexcept
    {
        return ownerId;
    }

    ~SharedSession()
    {
        std::vector<NSUniquePtr<NSURLSession>> sessionsToInvalidate;

        {
            const std::scoped_lock lock { mutex };
            lifecycle = Lifecycle::shuttingDown;
            setAcceptingGenerationLocked (nullptr);

            for (auto& generation : generations)
                if (auto sessionToInvalidate = beginInvalidationLocked (*generation))
                    sessionsToInvalidate.push_back (std::move (sessionToInvalidate));
        }

        for (auto& sessionToInvalidate : sessionsToInvalidate)
            [sessionToInvalidate.get() finishTasksAndInvalidate];

        sessionsToInvalidate.clear();

        std::unique_lock lock { mutex };
        condvar.wait (lock, [&] { return generations.empty() && activeCallbackCount == 0; });
        DelegateClass::setState (delegate.get(), nullptr);
    }

    RegisteredSessionTask addTask (NSURLRequest* request, SessionListener* listener)
    {
        NSUniquePtr<NSURLSession> sessionToInvalidate;
        RegisteredSessionTask result;
        const auto requestUrlHash = detail::getWebInputStreamDiagnosticRequestUrlHash (request);

        {
            const std::scoped_lock lock { mutex };

            if (lifecycle != Lifecycle::running || request == nullptr || listener == nullptr)
                return {};

            auto* generation = acceptingGeneration;

            if (generation == nullptr)
                generation = createGenerationLocked();

            if (generation == nullptr)
                return {};

            NSUniquePtr<NSURLSessionTask> task
            {
                [[generation->session.get() dataTaskWithRequest: request] retain]
            };

            if (task != nullptr)
            {
                const auto identifier = [task.get() taskIdentifier];
                const auto inserted = generation->listeners.emplace (
                    identifier, ListenerRecord { listener, 0, false, requestUrlHash });

                if (inserted.second)
                {
                    ++liveListenerCount;
                    ++totalRegisteredTaskCount;
                    result.task = std::move (task);
                    result.generation = generation->generation;
                    result.identifier = identifier;
                    result.requestUrlHash = requestUrlHash;
                }
                else
                {
                    jassertfalse;
                }
            }

            if (! result && generation->listeners.empty())
                sessionToInvalidate = beginInvalidationLocked (*generation);
        }

        if (sessionToInvalidate != nullptr)
            [sessionToInvalidate.get() finishTasksAndInvalidate];

        if (result)
        {
            detail::publishWebInputStreamDiagnosticEvent (
                { detail::WebInputStreamDiagnosticEventType::taskRegistered,
                  result.generation,
                  static_cast<std::uint64_t> (result.identifier),
                  result.requestUrlHash,
                  0,
                  ownerId });
        }

        return result;
    }

    void removeTask (const RegisteredSessionTask& task)
    {
        if (! task)
            return;

        std::unique_lock lock { mutex };
        condvar.wait (lock,
                      [&]
                      {
                          const auto* generation = findGenerationLocked (task.generation);

                          return (generation == nullptr
                                  || generation->listeners.find (task.identifier) == generation->listeners.end())
                              && inFlightDiagnosticLifecyclePublicationCount == 0;
                      });
    }

    void acquireTokenLease() noexcept
    {
        const std::scoped_lock lock { mutex };
        ++liveTokenLeaseCount;
    }

    void releaseTokenLease (const RegisteredSessionTask& task) noexcept
    {
        detail::WebInputStreamDiagnosticEvent releasedEvent;
        bool released = false;

        {
            const std::scoped_lock lock { mutex };
            jassert (liveTokenLeaseCount > 0);

            if (liveTokenLeaseCount > 0)
            {
                ++inFlightDiagnosticLifecyclePublicationCount;
                --liveTokenLeaseCount;
                ++totalReleasedTokenCount;
                releasedEvent.type = detail::WebInputStreamDiagnosticEventType::tokenReleased;
                releasedEvent.generation = task.generation;
                releasedEvent.taskIdentifier = static_cast<std::uint64_t> (task.identifier);
                releasedEvent.requestUrlHash = task.requestUrlHash;
                releasedEvent.ownerId = ownerId;
                released = true;
            }
        }

        if (released)
        {
            detail::publishWebInputStreamDiagnosticEvent (releasedEvent);

            const std::scoped_lock lock { mutex };
            jassert (inFlightDiagnosticLifecyclePublicationCount > 0);
            --inFlightDiagnosticLifecyclePublicationCount;
            condvar.notify_all();
        }
    }

    detail::WebInputStreamDiagnosticSnapshot getDiagnosticSnapshot()
    {
        const std::scoped_lock lock { mutex };
        return getDiagnosticSnapshotLocked();
    }

    detail::WebInputStreamDiagnosticSnapshot waitForDiagnosticQuiescence (std::chrono::milliseconds timeout)
    {
        std::unique_lock lock { mutex };
        condvar.wait_for (lock,
                          timeout,
                          [&]
                          {
                              return liveListenerCount == 0
                                  && activeCallbackCount == 0
                                  && liveTokenLeaseCount == 0
                                  && inFlightDiagnosticLifecyclePublicationCount == 0;
                          });
        return getDiagnosticSnapshotLocked();
    }

private:
    void didBecomeInvalid (NSURLSession* session, [[maybe_unused]] NSError* error)
    {
        ScopedCallback callback { *this };

       #if JUCE_DEBUG
        if (error != nullptr)
            DBG (nsStringToJuce ([error description]));
       #endif

        std::vector<ListenerCallback> toNotify;
        std::unique_ptr<SessionGeneration> generationToRelease;
        std::uint64_t generationId = 0;

        {
            const std::scoped_lock lock { mutex };
            auto* generation = findGenerationLocked (session);

            if (generation == nullptr || generation->invalidationCallbackReceived)
                return;

            generationId = generation->generation;
            generation->invalidationRequested = true;
            generation->invalidationCallbackReceived = true;

            if (acceptingGeneration == generation)
                setAcceptingGenerationLocked (nullptr);

            for (auto& [identifier, record] : generation->listeners)
            {
                if (record.terminalCallbackStarted)
                    continue;

                record.terminalCallbackStarted = true;
                ++record.activeCallbacks;
                toNotify.emplace_back (this, generationId, identifier, record.listener);
            }

            generationToRelease = takeDrainedGenerationLocked (generationId);
            condvar.notify_all();
        }

        for (auto& listener : toNotify)
            listener->didComplete (error);
    }

    void didComplete (NSURLSession* session,
                      NSURLSessionTask* task,
                      [[maybe_unused]] NSError* error)
    {
        ScopedCallback callback { *this };

       #if JUCE_DEBUG
        if (error != nullptr)
            DBG (nsStringToJuce ([error description]));
       #endif

        auto listener = beginTerminalCallback (session, task);

        if (listener)
            listener->didComplete (error);
    }

    void didReceiveResponse (NSURLSession* session,
                             NSURLSessionTask* task,
                             NSURLResponse* response,
                             void (^completionHandler) (NSURLSessionResponseDisposition))
    {
        ScopedCallback callback { *this };

        if (auto listener = getListener (session, task))
            listener->didReceiveResponse (response, completionHandler);
        else
            completionHandler (NSURLSessionResponseCancel);
    }

    void didReceiveData (NSURLSession* session, NSURLSessionTask* task, NSData* newData)
    {
        ScopedCallback callback { *this };

        if (auto listener = getListener (session, task))
            listener->didReceiveData (newData);
    }

    void didSendBodyData (NSURLSession* session,
                          NSURLSessionTask* task,
                          int64_t totalBytesWritten)
    {
        ScopedCallback callback { *this };

        if (auto listener = getListener (session, task))
            listener->didSendBodyData (totalBytesWritten);
    }

    void willPerformHTTPRedirection (NSURLSession* session,
                                     NSURLSessionTask* task,
                                     NSURLRequest* urlRequest,
                                     void (^completionHandler) (NSURLRequest *))
    {
        ScopedCallback callback { *this };

        if (auto listener = getListener (session, task))
            listener->willPerformHTTPRedirection (urlRequest, completionHandler);
        else
            completionHandler (nil);
    }

    ListenerCallback getListener (NSURLSession* session, NSURLSessionTask* task)
    {
        const std::scoped_lock lock { mutex };
        auto* generation = findGenerationLocked (session);

        if (generation == nullptr)
            return {};

        const auto identifier = [task taskIdentifier];
        const auto iter = generation->listeners.find (identifier);

        if (iter == generation->listeners.end() || iter->second.terminalCallbackStarted)
            return {};

        ++iter->second.activeCallbacks;
        return { this, generation->generation, identifier, iter->second.listener };
    }

    ListenerCallback beginTerminalCallback (NSURLSession* session, NSURLSessionTask* task)
    {
        const std::scoped_lock lock { mutex };
        auto* generation = findGenerationLocked (session);

        if (generation == nullptr)
            return {};

        const auto identifier = [task taskIdentifier];
        const auto iter = generation->listeners.find (identifier);

        if (iter == generation->listeners.end() || iter->second.terminalCallbackStarted)
            return {};

        iter->second.terminalCallbackStarted = true;
        ++iter->second.activeCallbacks;
        return { this, generation->generation, identifier, iter->second.listener };
    }

    SessionGeneration* createGenerationLocked()
    {
        if (lifecycle != Lifecycle::running || nextGeneration == 0)
            return nullptr;

        const auto generationId = nextGeneration;
        nextGeneration = generationId == std::numeric_limits<std::uint64_t>::max()
                       ? 0
                       : generationId + 1;

        auto generation = std::make_unique<SessionGeneration> (generationId, delegate.get());

        if (generation->session == nullptr)
            return nullptr;

        auto* result = generation.get();
        generations.push_back (std::move (generation));
        ++totalGenerationConstructions;
        ++liveGenerationCount;
        setAcceptingGenerationLocked (result);
        return result;
    }

    NSUniquePtr<NSURLSession> beginInvalidationLocked (SessionGeneration& generation)
    {
        if (generation.invalidationRequested)
            return {};

        generation.invalidationRequested = true;

        if (acceptingGeneration == &generation)
            setAcceptingGenerationLocked (nullptr);

        return NSUniquePtr<NSURLSession> { [generation.session.get() retain] };
    }

    SessionGeneration* findGenerationLocked (NSURLSession* session)
    {
        const auto iter = std::find_if (generations.begin(),
                                        generations.end(),
                                        [session] (const auto& generation)
                                        {
                                            return generation->session.get() == session;
                                        });
        return iter != generations.end() ? iter->get() : nullptr;
    }

    SessionGeneration* findGenerationLocked (std::uint64_t generationId)
    {
        const auto iter = std::find_if (generations.begin(),
                                        generations.end(),
                                        [generationId] (const auto& generation)
                                        {
                                            return generation->generation == generationId;
                                        });
        return iter != generations.end() ? iter->get() : nullptr;
    }

    void setAcceptingGenerationLocked (SessionGeneration* generation) noexcept
    {
        acceptingGeneration = generation;
        acceptingGenerationId = generation != nullptr ? generation->generation : 0;
    }

    detail::WebInputStreamDiagnosticSnapshot getDiagnosticSnapshotLocked() const noexcept
    {
        detail::WebInputStreamDiagnosticSnapshot result;
        result.ownerId = ownerId;
        result.ownerConstructionCount = webInputStreamDiagnosticOwnerConstructionCount.load (std::memory_order_acquire);
        result.acceptingGenerationId = acceptingGenerationId;
        result.totalGenerationConstructions = totalGenerationConstructions;
        result.liveGenerationCount = liveGenerationCount;
        result.liveListenerCount = liveListenerCount;
        result.activeCallbackCount = activeCallbackCount;
        result.inFlightDiagnosticLifecyclePublicationCount = inFlightDiagnosticLifecyclePublicationCount;
        result.liveTokenLeaseCount = liveTokenLeaseCount;
        result.totalRegisteredTaskCount = totalRegisteredTaskCount;
        result.totalRetiredTaskCount = totalRetiredTaskCount;
        result.totalReleasedTokenCount = totalReleasedTokenCount;
        return result;
    }

    std::unique_ptr<SessionGeneration> takeDrainedGenerationLocked (std::uint64_t generationId)
    {
        const auto iter = std::find_if (generations.begin(),
                                        generations.end(),
                                        [generationId] (const auto& generation)
                                        {
                                            return generation->generation == generationId;
                                        });

        if (iter == generations.end()
            || ! (*iter)->invalidationCallbackReceived
            || ! (*iter)->listeners.empty())
        {
            return {};
        }

        if (acceptingGeneration == iter->get())
            setAcceptingGenerationLocked (nullptr);

        auto result = std::move (*iter);
        generations.erase (iter);
        jassert (liveGenerationCount > 0);
        --liveGenerationCount;
        return result;
    }

    struct DelegateClass final : public ObjCClass<NSObject<NSURLSessionTaskDelegate>>
    {
        DelegateClass()
            : ObjCClass ("JUCE_URLDelegate_")
        {
            addIvar<SharedSession*> ("state");

            addMethod (@selector (URLSession:didBecomeInvalidWithError:),
                       [] (id self, SEL, NSURLSession* session, NSError* error)
                       {
                           if (auto* state = getState (self))
                               state->didBecomeInvalid (session, error);
                       });

            addMethod (@selector (URLSession:dataTask:didReceiveResponse:completionHandler:),
                       [] (id self,
                           SEL,
                           NSURLSession* session,
                           NSURLSessionDataTask* task,
                           NSURLResponse* response,
                           void (^completionHandler) (NSURLSessionResponseDisposition))
                       {
                           if (auto* state = getState (self))
                               state->didReceiveResponse (session, task, response, completionHandler);
                           else
                               completionHandler (NSURLSessionResponseCancel);
                       });

            addMethod (@selector (URLSession:dataTask:didReceiveData:),
                       [] (id self, SEL, NSURLSession* session, NSURLSessionDataTask* task, NSData* newData)
                       {
                           if (auto* state = getState (self))
                               state->didReceiveData (session, task, newData);
                       });

            addMethod (@selector (URLSession:task:didSendBodyData:totalBytesSent:totalBytesExpectedToSend:),
                       [] (id self,
                           SEL,
                           NSURLSession* session,
                           NSURLSessionTask* task,
                           int64_t,
                           int64_t totalBytesWritten,
                           int64_t)
                       {
                           if (auto* state = getState (self))
                               state->didSendBodyData (session, task, totalBytesWritten);
                       });

            addMethod (@selector (URLSession:task:willPerformHTTPRedirection:newRequest:completionHandler:),
                       [] (id self,
                           SEL,
                           NSURLSession* session,
                           NSURLSessionTask* task,
                           NSHTTPURLResponse*,
                           NSURLRequest* req,
                           void (^completionHandler) (NSURLRequest *))
                       {
                           if (auto* state = getState (self))
                               state->willPerformHTTPRedirection (session, task, req, completionHandler);
                           else
                               completionHandler (nil);
                       });

            addMethod (@selector (URLSession:task:didCompleteWithError:),
                       [] (id self, SEL, NSURLSession* session, NSURLSessionTask* task, NSError* error)
                       {
                           if (auto* state = getState (self))
                               state->didComplete (session, task, error);
                       });

            registerClass();
        }

        static void setState (NSObject* self, SharedSession* state)  { object_setInstanceVariable (self, "state", state); }
        static SharedSession* getState (NSObject* self)              { return getIvar<SharedSession*> (self, "state"); }
    };

    const std::uint64_t ownerId;
    std::mutex mutex;
    std::condition_variable condvar;

    DelegateClass delegateClass;
    NSUniquePtr<NSObject<NSURLSessionTaskDelegate>> delegate { [delegateClass.createInstance() init] };
    std::vector<std::unique_ptr<SessionGeneration>> generations;
    SessionGeneration* acceptingGeneration = nullptr;
    std::uint64_t acceptingGenerationId = 0;
    std::uint64_t nextGeneration = 1;
    std::uint64_t totalGenerationConstructions = 0;
    std::uint64_t liveGenerationCount = 0;
    std::uint64_t liveListenerCount = 0;
    std::uint64_t inFlightDiagnosticLifecyclePublicationCount = 0;
    std::uint64_t liveTokenLeaseCount = 0;
    std::uint64_t totalRegisteredTaskCount = 0;
    std::uint64_t totalRetiredTaskCount = 0;
    std::uint64_t totalReleasedTokenCount = 0;
    Lifecycle lifecycle = Lifecycle::running;
    std::uint64_t activeCallbackCount = 0;
};

void SharedSession::ListenerCallback::reset() noexcept
{
    if (auto* callbackOwner = std::exchange (owner, nullptr))
        callbackOwner->releaseListenerCallback (generation, identifier);
}

SharedSession::ListenerCallback::~ListenerCallback()
{
    reset();
}

void SharedSession::releaseListenerCallback (std::uint64_t generationId,
                                             NSUInteger identifier) noexcept
{
    std::unique_ptr<SessionGeneration> generationToRelease;
    detail::WebInputStreamDiagnosticEvent retiredEvent;
    bool retired = false;
    bool shouldNotify = false;

    {
        const std::scoped_lock lock { mutex };
        auto* generation = findGenerationLocked (generationId);

        if (generation != nullptr)
        {
            shouldNotify = true;
            const auto iter = generation->listeners.find (identifier);

            if (iter != generation->listeners.end())
            {
                jassert (iter->second.activeCallbacks > 0);
                --iter->second.activeCallbacks;

                if (iter->second.activeCallbacks == 0
                    && iter->second.terminalCallbackStarted)
                {
                    ++inFlightDiagnosticLifecyclePublicationCount;
                    retiredEvent.type = detail::WebInputStreamDiagnosticEventType::taskRetired;
                    retiredEvent.generation = generationId;
                    retiredEvent.taskIdentifier = static_cast<std::uint64_t> (identifier);
                    retiredEvent.requestUrlHash = iter->second.requestUrlHash;
                    retiredEvent.ownerId = ownerId;
                    generation->listeners.erase (iter);
                    jassert (liveListenerCount > 0);
                    --liveListenerCount;
                    ++totalRetiredTaskCount;
                    retired = true;
                }
            }

            generationToRelease = takeDrainedGenerationLocked (generationId);
        }
    }

    if (retired)
    {
        detail::publishWebInputStreamDiagnosticEvent (retiredEvent);

        const std::scoped_lock lock { mutex };
        jassert (inFlightDiagnosticLifecyclePublicationCount > 0);
        --inFlightDiagnosticLifecyclePublicationCount;
        condvar.notify_all();
    }

    if (shouldNotify)
        condvar.notify_all();
}

/** Keep one SharedSession alive for the process lifetime. The longer-lived
    owner amortizes healthy generation and CFNetwork transport state across
    sequential WebInputStreams; each TaskToken retains a strong handle to the
    exact owner used to register its task, preserving lifetime and provenance. */
static std::shared_ptr<SharedSession> getProcessSharedSession()
{
    static const auto session = std::make_shared<SharedSession>();
    return session;
}

namespace detail
{
WebInputStreamDiagnosticSnapshot getWebInputStreamDiagnosticSnapshot()
{
    return getProcessSharedSession()->getDiagnosticSnapshot();
}

WebInputStreamDiagnosticSnapshot waitForWebInputStreamDiagnosticQuiescence (std::chrono::milliseconds timeout)
{
    return getProcessSharedSession()->waitForDiagnosticQuiescence (timeout);
}
} // namespace detail

class TaskToken
{
public:
    TaskToken() = default;

    explicit TaskToken (std::shared_ptr<SharedSession> sessionToUse,
                        NSURLRequest* request,
                        SessionListener* l)
        : session (std::move (sessionToUse))
    {
        if (session == nullptr)
            return;

        task = session->addTask (request, l);

        if (! task)
        {
            session.reset();
            return;
        }

        session->acquireTokenLease();
        tokenLeaseHeld = true;

        if (auto* t = task.task.get())
            [t resume];
    }

    TaskToken (TaskToken&& other) noexcept
        : session (std::move (other.session)),
          task (std::move (other.task)),
          tokenLeaseHeld (std::exchange (other.tokenLeaseHeld, false))
    {
    }

    ~TaskToken()
    {
        reset();
    }

    TaskToken& operator= (TaskToken&& other) noexcept
    {
        if (this != &other)
        {
            reset();

            session = std::move (other.session);
            task = std::move (other.task);
            tokenLeaseHeld = std::exchange (other.tokenLeaseHeld, false);
        }

        return *this;
    }

    explicit operator bool() const noexcept { return static_cast<bool> (task); }

    void cancel()
    {
        if (auto* toCancel = task.task.get())
            [toCancel cancel];
    }

    void publishResponseBodyWait (std::int64_t bytesConsumedByRead) const noexcept
    {
        if (! task || session == nullptr)
            return;

        detail::publishWebInputStreamDiagnosticEvent (
            { detail::WebInputStreamDiagnosticEventType::responseBodyWait,
              task.generation,
              static_cast<std::uint64_t> (task.identifier),
              task.requestUrlHash,
              bytesConsumedByRead,
              session != nullptr ? session->getOwnerId() : 0 });
    }

private:
    void reset()
    {
        if (task && session != nullptr)
        {
            session->removeTask (task);

            if (std::exchange (tokenLeaseHeld, false))
                session->releaseTokenLease (task);
        }
        else
        {
            tokenLeaseHeld = false;
        }

        task = {};
        session.reset();
    }

    std::shared_ptr<SharedSession> session;
    RegisteredSessionTask task;
    bool tokenLeaseHeld = false;
};

//==============================================================================
class URLConnectionState final : private SessionListener
{
public:
    URLConnectionState (NSUniquePtr<NSURLRequest> req, const int maxRedirects)
        : request (std::move (req)),
          numRedirects (maxRedirects)
    {
    }

    ~URLConnectionState() override
    {
        cancel();
    }

    void cancel()
    {
        const std::scoped_lock lock { mutex };
        token.cancel();
    }

    int64 getContentLength() const noexcept
    {
        const std::scoped_lock lock { mutex };
        return contentLength;
    }

    NSUniquePtr<NSDictionary> getHeaders() const noexcept
    {
        const std::scoped_lock lock { mutex };
        return NSUniquePtr<NSDictionary> { [headers.get() copy] };
    }

    int getStatusCode() const noexcept
    {
        const std::scoped_lock lock { mutex };
        return statusCode;
    }

    bool start (WebInputStream& inputStream, WebInputStream::Listener* listener)
    {
        TaskToken newToken { getProcessSharedSession(), request.get(), this };

        if (! newToken)
            return false;

        std::unique_lock lock { mutex };
        token = std::move (newToken);

        while (! condvar.wait_for (lock,
                                   std::chrono::milliseconds { 1 },
                                   [&] { return state != State::beforeStart; }))
        {
            if (listener != nullptr
                && ! listener->postDataSendProgress (inputStream,
                                                     (int) latestTotalBytes,
                                                     (int) [[request.get() HTTPBody] length]))
            {
                return false;
            }
        }

        return true;
    }

    int read (char* dest, int numBytes)
    {
        int numDone = 0;

        while (numBytes > 0)
        {
            std::unique_lock lock { mutex };

            const auto getNumAvailable = [&] { return jmin (numBytes, (int) [data.get() length]); };

            if (getNumAvailable() == 0 && state != State::requestFinished)
            {
                // This event is published while holding the same mutex that
                // cancel() must acquire, immediately before condvar.wait()
                // atomically releases it. A teardown that observes the event
                // therefore cannot cancel ahead of this production wait.
                token.publishResponseBodyWait (numDone);
            }

            condvar.wait (lock, [&] { return getNumAvailable() > 0 || state == State::requestFinished; });

            const auto available = getNumAvailable();

            if (available <= 0)
                break;

            [data.get() getBytes: dest length: (NSUInteger) available];
            [data.get() replaceBytesInRange: NSMakeRange (0, (NSUInteger) available) withBytes: nil length: 0];

            numDone += available;
            numBytes -= available;
            dest += available;
        }

        return numDone;
    }

private:
    void didComplete (NSError*) override
    {
        {
            const std::scoped_lock lock { mutex };
            state = State::requestFinished;
        }

        condvar.notify_one();
    }

    void didReceiveResponse (NSURLResponse* response, void (^completionHandler) (NSURLSessionResponseDisposition)) override
    {
        {
            const std::scoped_lock lock { mutex };

            contentLength = [response expectedContentLength];

            if ([response isKindOfClass: [NSHTTPURLResponse class]])
            {
                auto httpResponse = (NSHTTPURLResponse*) response;
                headers.reset ([[httpResponse allHeaderFields] retain]);
                statusCode = (int) [httpResponse statusCode];
            }

            if (state == State::beforeStart)
                state = State::started;
        }

        condvar.notify_one();
        completionHandler (NSURLSessionResponseAllow);
    }

    void didReceiveData (NSData* newData) override
    {
        {
            const std::scoped_lock lock { mutex };
            [data.get() appendData: newData];

            if (state == State::beforeStart)
                state = State::started;
        }

        condvar.notify_one();
    }

    void didSendBodyData (int64_t totalBytesWritten) override
    {
        const std::scoped_lock lock { mutex };
        latestTotalBytes = totalBytesWritten;
    }

    void willPerformHTTPRedirection (NSURLRequest* urlRequest, void (^completionHandler) (NSURLRequest *)) override
    {
        // No lock required here because numRedirects is only accessed from the session's work queue
        // after the task has started.
        completionHandler (--numRedirects >= 0 ? urlRequest : nil);
    }

    enum class State
    {
        beforeStart,
        started,
        requestFinished
    };

    mutable std::mutex mutex;
    std::condition_variable condvar;

    NSUniquePtr<NSDictionary> headers;
    NSUniquePtr<NSURLRequest> request;
    NSUniquePtr<NSMutableData> data { [[NSMutableData data] retain] };

    int64 latestTotalBytes = 0;
    int64 contentLength = -1;
    int statusCode = 0;
    int numRedirects = 0;
    State state = State::beforeStart;

    TaskToken token;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (URLConnectionState)
};

//==============================================================================
#if JUCE_IOS
struct BackgroundDownloadTask final : public URL::DownloadTask
{
    BackgroundDownloadTask (const URL& urlToUse,
                            const File& targetLocationToUse,
                            const URL::DownloadTaskOptions& options)
         : listener (options.listener),
           uniqueIdentifier (String (urlToUse.toString (true).hashCode64()) + String (Random().nextInt64()))
    {
        targetLocation = targetLocationToUse;
        downloaded = -1;

        static DelegateClass cls;
        delegate = [cls.createInstance() init];
        DelegateClass::setState (delegate, this);

        activeSessions.set (uniqueIdentifier, this);
        auto nsUrl = [NSURL URLWithString: juceStringToNS (urlToUse.toString (true))];

        jassert (nsUrl != nullptr);

        JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wnullable-to-nonnull-conversion")
        NSMutableURLRequest* request = [[NSMutableURLRequest alloc] initWithURL: nsUrl];
        JUCE_END_IGNORE_WARNINGS_GCC_LIKE

        if (options.usePost)
            [request setHTTPMethod: @"POST"];

        StringArray headerLines;
        headerLines.addLines (options.extraHeaders);
        headerLines.removeEmptyStrings (true);

        for (int i = 0; i < headerLines.size(); ++i)
        {
            String key   = headerLines[i].upToFirstOccurrenceOf (":", false, false).trim();
            String value = headerLines[i].fromFirstOccurrenceOf (":", false, false).trim();

            if (key.isNotEmpty() && value.isNotEmpty())
                [request addValue: juceStringToNS (value) forHTTPHeaderField: juceStringToNS (key)];
        }

        auto* configuration = [NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier: juceStringToNS (uniqueIdentifier)];

        if (options.sharedContainer.isNotEmpty())
            [configuration setSharedContainerIdentifier: juceStringToNS (options.sharedContainer)];

        session = [NSURLSession sessionWithConfiguration: configuration
                                                delegate: delegate
                                           delegateQueue: nullptr];

        if (session != nullptr)
            downloadTask = [session downloadTaskWithRequest:request];

        // Workaround for an Apple bug. See https://github.com/AFNetworking/AFNetworking/issues/2334
        [request HTTPBody];

        [request release];
    }

    ~BackgroundDownloadTask()
    {
        activeSessions.remove (uniqueIdentifier);

        if (httpCode != -1)
            httpCode = 500;

        finished = true;
        connectionEvent.signal();

        [session invalidateAndCancel];
        while (! hasBeenDestroyed)
            destroyEvent.wait();

        [delegate release];
    }

    bool initOK()
    {
        return (downloadTask != nullptr);
    }

    bool connect()
    {
        [downloadTask resume];
        while (downloaded == -1 && finished == false)
            connectionEvent.wait();

        connectFinished = true;
        return ! error;
    }

    //==============================================================================
    URL::DownloadTask::Listener* listener;
    NSObject<NSURLSessionDelegate>* delegate = nil;
    NSURLSession* session = nil;
    NSURLSessionDownloadTask* downloadTask = nil;
    bool connectFinished = false, hasBeenDestroyed = false;
    Atomic<int> calledComplete;
    WaitableEvent connectionEvent, destroyEvent;
    String uniqueIdentifier;

    static HashMap<String, BackgroundDownloadTask*, DefaultHashFunctions, CriticalSection> activeSessions;

    void didWriteData (int64 totalBytesWritten, int64 totalBytesExpectedToWrite)
    {
        downloaded = totalBytesWritten;

        if (contentLength == -1)
            contentLength = totalBytesExpectedToWrite;

        if (connectFinished && error == false && finished == false && listener != nullptr)
            listener->progress (this, totalBytesWritten, contentLength);

        connectionEvent.signal();
    }

    void didFinishDownloadingToURL (NSURL* location)
    {
        auto* fileManager = [NSFileManager defaultManager];
        error = ([fileManager moveItemAtURL: location
                                      toURL: createNSURLFromFile (targetLocation)
                                      error: nil] == NO);
        httpCode = 200;
        finished = true;

        connectionEvent.signal();

        if (listener != nullptr && calledComplete.exchange (1) == 0)
        {
            if (contentLength > 0 && downloaded < contentLength)
            {
                downloaded = contentLength;
                listener->progress (this, downloaded, contentLength);
            }

            listener->finished (this, !error);
        }
    }

    static int getHTTPErrorCode (NSError* nsError)
    {
        // see https://developer.apple.com/reference/foundation/nsurlsessiondownloadtask?language=objc
        switch ([nsError code])
        {
            case NSURLErrorUserAuthenticationRequired:  return 401;
            case NSURLErrorNoPermissionsToReadFile:     return 403;
            case NSURLErrorFileDoesNotExist:            return 404;
            default:                                    return 500;
        }
    }

    void didCompleteWithError (NSError* nsError)
    {
        if (calledComplete.exchange (1) == 0)
        {
            httpCode = nsError != nil ? getHTTPErrorCode (nsError) : -1;
            error = true;
            finished = true;

            if (listener != nullptr)
                listener->finished (this, ! error);
        }

        connectionEvent.signal();
    }

    void didBecomeInvalidWithError()
    {
        hasBeenDestroyed = true;
        destroyEvent.signal();
    }

    //==============================================================================
    void notify()
    {
        if (downloadTask == nullptr) return;

        if (NSError* error = [downloadTask error])
        {
            didCompleteWithError (error);
        }
        else
        {
            const int64 contentLength = [downloadTask countOfBytesExpectedToReceive];

            if ([downloadTask state] == NSURLSessionTaskStateCompleted)
                didWriteData (contentLength, contentLength);
            else
                didWriteData ([downloadTask countOfBytesReceived], contentLength);
        }
    }

    static void invokeNotify (const String& identifier)
    {
        ScopedLock lock (activeSessions.getLock());

        if (auto* task = activeSessions[identifier])
            task->notify();
    }

    //==============================================================================
    struct DelegateClass final : public ObjCClass<NSObject<NSURLSessionDelegate>>
    {
        DelegateClass()  : ObjCClass<NSObject<NSURLSessionDelegate>> ("JUCE_URLDelegate_")
        {
            addIvar<BackgroundDownloadTask*> ("state");

            addMethod (@selector (URLSession:downloadTask:didWriteData:totalBytesWritten:totalBytesExpectedToWrite:), didWriteData);
            addMethod (@selector (URLSession:downloadTask:didFinishDownloadingToURL:),  didFinishDownloadingToURL);
            addMethod (@selector (URLSession:task:didCompleteWithError:),               didCompleteWithError);
            addMethod (@selector (URLSession:didBecomeInvalidWithError:),               didBecomeInvalidWithError);

            registerClass();
        }

        static void setState (id self, BackgroundDownloadTask* state)  { object_setInstanceVariable (self, "state", state); }
        static BackgroundDownloadTask* getState (id self)              { return getIvar<BackgroundDownloadTask*> (self, "state"); }

    private:
        static void didWriteData (id self, SEL, NSURLSession*, NSURLSessionDownloadTask*, int64_t, int64_t totalBytesWritten, int64_t totalBytesExpectedToWrite)
        {
            if (auto state = getState (self))
                state->didWriteData (totalBytesWritten, totalBytesExpectedToWrite);
        }

        static void didFinishDownloadingToURL (id self, SEL, NSURLSession*, NSURLSessionDownloadTask*, NSURL* location)
        {
            if (auto state = getState (self))
                state->didFinishDownloadingToURL (location);
        }

        static void didCompleteWithError (id self, SEL, NSURLSession*, NSURLSessionTask*, NSError* nsError)
        {
            if (auto state = getState (self))
                state->didCompleteWithError (nsError);
        }

        static void didBecomeInvalidWithError (id self, SEL, NSURLSession*, NSURLSessionTask*, NSError*)
        {
            if (auto state = getState (self))
                state->didBecomeInvalidWithError();
        }
    };
};

HashMap<String, BackgroundDownloadTask*, DefaultHashFunctions, CriticalSection> BackgroundDownloadTask::activeSessions;

std::unique_ptr<URL::DownloadTask> URL::downloadToFile (const File& targetLocation, const DownloadTaskOptions& options)
{
    auto downloadTask = std::make_unique<BackgroundDownloadTask> (*this, targetLocation, options);

    if (downloadTask->initOK() && downloadTask->connect())
        return downloadTask;

    return nullptr;
}

void URL::DownloadTask::juce_iosURLSessionNotify (const String& identifier)
{
    BackgroundDownloadTask::invokeNotify (identifier);
}
#else
std::unique_ptr<URL::DownloadTask> URL::downloadToFile (const File& targetLocation, const DownloadTaskOptions& options)
{
    return URL::DownloadTask::createFallbackDownloader (*this, targetLocation, options);
}
#endif

//==============================================================================
class WebInputStream::Pimpl
{
public:
    Pimpl (WebInputStream& pimplOwner, const URL& urlToUse, bool addParametersToBody)
        : owner (pimplOwner),
          url (urlToUse),
          addParametersToRequestBody (addParametersToBody),
          hasBodyDataToSend (addParametersToRequestBody || url.hasBodyDataToSend()),
          httpRequestCmd (hasBodyDataToSend ? "POST" : "GET")
    {
    }

    ~Pimpl()
    {
        connection.reset();
    }

    bool connect (WebInputStream::Listener* webInputListener, [[maybe_unused]] int numRetries = 0)
    {
        {
            const ScopedLock lock (createConnectionLock);

            if (hasBeenCancelled)
                return false;

            createConnection();
        }

        if (! connection.has_value())
            return false;

        if (! connection->start (owner, webInputListener))
        {
            connection.reset();
            return false;
        }

        const auto connectionHeaders = connection->getHeaders();

        if (connectionHeaders == nullptr)
            return false;

        statusCode = connection->getStatusCode();

        NSEnumerator* enumerator = [connectionHeaders.get() keyEnumerator];

        while (NSString* key = [enumerator nextObject])
            responseHeaders.set (nsStringToJuce (key),
                                 nsStringToJuce ((NSString*) [connectionHeaders.get() objectForKey: key]));

        return true;
    }

    void cancel()
    {
        const ScopedLock lock (createConnectionLock);

        if (connection.has_value())
            connection->cancel();

        hasBeenCancelled = true;
    }

    //==============================================================================
    // WebInputStream methods
    void withExtraHeaders (const String& extraHeaders)
    {
        if (! headers.endsWithChar ('\n') && headers.isNotEmpty())
            headers << "\r\n";

        headers << extraHeaders;

        if (! headers.endsWithChar ('\n') && headers.isNotEmpty())
            headers << "\r\n";
    }

    void withCustomRequestCommand (const String& customRequestCommand)    { httpRequestCmd = customRequestCommand; }
    void withConnectionTimeout (int timeoutInMs)                          { timeOutMs = timeoutInMs; }
    void withNumRedirectsToFollow (int maxRedirectsToFollow)              { numRedirectsToFollow = maxRedirectsToFollow; }
    StringPairArray getRequestHeaders() const                             { return WebInputStream::parseHttpHeaders (headers); }
    StringPairArray getResponseHeaders() const                            { return responseHeaders; }
    int getStatusCode() const                                             { return statusCode; }

    //==============================================================================
    bool isError() const                { return (! connection.has_value() || connection->getHeaders() == nullptr); }
    int64 getTotalLength()              { return ! connection.has_value() ? -1 : connection->getContentLength(); }
    bool isExhausted()                  { return finished; }
    int64 getPosition()                 { return position; }

    int read (void* buffer, int bytesToRead)
    {
        jassert (buffer != nullptr && bytesToRead >= 0);

        if (finished || isError())
            return 0;

        JUCE_AUTORELEASEPOOL
        {
            const int bytesRead = connection->read (static_cast<char*> (buffer), bytesToRead);
            position += bytesRead;

            if (bytesRead == 0)
                finished = true;

            return bytesRead;
        }
    }

    bool setPosition (int64 wantedPos)
    {
        if (wantedPos != position)
        {
            finished = false;

            if (wantedPos < position)
                return false;

            auto numBytesToSkip = wantedPos - position;
            auto skipBufferSize = (int) jmin (numBytesToSkip, (int64) 16384);
            HeapBlock<char> temp (skipBufferSize);

            while (numBytesToSkip > 0 && ! isExhausted())
                numBytesToSkip -= read (temp, (int) jmin (numBytesToSkip, (int64) skipBufferSize));
        }

        return true;
    }

    int statusCode = 0;

private:
    WebInputStream& owner;
    URL url;
    std::optional<URLConnectionState> connection;
    String headers;
    MemoryBlock postData;
    int64 position = 0;
    bool finished = false;
    const bool addParametersToRequestBody, hasBodyDataToSend;
    int timeOutMs = 0;
    int numRedirectsToFollow = 5;
    String httpRequestCmd;
    StringPairArray responseHeaders;
    CriticalSection createConnectionLock;
    bool hasBeenCancelled = false;

    void createConnection()
    {
        jassert (! connection.has_value());

        NSUniquePtr<NSURL> nsURL { [[NSURL URLWithString: juceStringToNS (url.toString (! addParametersToRequestBody))] retain] };

        if (nsURL == nullptr)
            return;

        const auto timeOutSeconds = [this]
        {
            if (timeOutMs > 0)
                return timeOutMs / 1000.0;

            return timeOutMs < 0 ? std::numeric_limits<double>::infinity() : 60.0;
        }();

        NSUniquePtr<NSMutableURLRequest> req { [[NSMutableURLRequest requestWithURL: nsURL.get()
                                                                        cachePolicy: NSURLRequestReloadIgnoringLocalCacheData
                                                                    timeoutInterval: timeOutSeconds] retain] };

        if (req == nullptr)
            return;

        NSUniquePtr<NSString> httpMethod { [[NSString stringWithUTF8String: httpRequestCmd.toRawUTF8()] retain] };

        if (httpMethod == nullptr)
            return;

        [req.get() setHTTPMethod: httpMethod.get()];

        if (hasBodyDataToSend)
        {
            WebInputStream::createHeadersAndPostData (url,
                                                      headers,
                                                      postData,
                                                      addParametersToRequestBody);

            if (! postData.isEmpty())
                [req.get() setHTTPBody: [NSData dataWithBytes: postData.getData()
                                                       length: postData.getSize()]];
        }

        StringArray headerLines;
        headerLines.addLines (headers);
        headerLines.removeEmptyStrings (true);

        for (int i = 0; i < headerLines.size(); ++i)
        {
            auto key   = headerLines[i].upToFirstOccurrenceOf (":", false, false).trim();
            auto value = headerLines[i].fromFirstOccurrenceOf (":", false, false).trim();

            if (key.isNotEmpty() && value.isNotEmpty())
                [req.get() addValue: juceStringToNS (value) forHTTPHeaderField: juceStringToNS (key)];
        }

        // Workaround for an Apple bug. See https://github.com/AFNetworking/AFNetworking/issues/2334
        [req.get() HTTPBody];

        connection.emplace (std::move (req), numRedirectsToFollow);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pimpl)
};

} // namespace juce
