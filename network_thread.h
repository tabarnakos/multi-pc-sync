// *****************************************************************************
// Network Thread Class Definitions
// *****************************************************************************

#ifndef _NETWORK_THREAD_H_
#define _NETWORK_THREAD_H_

// Section 1: Includes
// C++ Standard Library
#include <functional>
#include <latch>
#include <thread>
#include <map>
#include <atomic>
#include <utility>

// Project Includes
#include "program_options.h"

// Section 2: Class Definitions
/**
 * Base class for network communication threads
 * Provides common functionality for both server and client threads
 */
class NetworkThread
{
public:
    /**
     * Context structure holding thread state and configuration
     */
    struct context
    {
        /**
         * Constructs a new context with program options
         * @param opts Program configuration options
         */
        context(ProgramOptions opts) :
            opts(std::move(opts)),
            latch(1),
            quit(false),
            active(false)
        {}
        ~context() = default;

        const ProgramOptions opts;     ///< Program configuration
        std::latch latch;             ///< Synchronization latch
        std::thread *thread{};          ///< Pointer to thread object
        std::atomic<bool> quit;       ///< Flag to signal thread termination
        std::atomic<bool> active;     ///< Flag indicating thread is running
        bool con_opened{};              ///< Flag indicating connection status
    };

    /**
     * Constructs a network thread
     * @param f Thread function to execute
     * @param opts Program configuration options
     */
    NetworkThread(const std::function<void(context &)> &func, const ProgramOptions &opts) : ctx(opts)
    { ctx.thread = new std::thread(func, std::ref(ctx)); }

    /**
     * Destructor - ensures thread is properly terminated and cleaned up
     */
    virtual ~NetworkThread()
    { kill(); ctx.active.wait(true); ctx.thread->join(); delete ctx.thread; }

    /**
     * Starts the thread by releasing the latch
     */
    void start() { ctx.latch.count_down(); }

    /**
     * Checks if the thread is currently active
     * @return true if thread is running
     */
    [[nodiscard]] bool isActive() const { return ctx.active.load() && (ctx.thread != nullptr) && !ctx.thread->joinable(); }

    /**
     * Waits for the thread to become active
     */
    [[nodiscard]] bool waitForActive() const
    {
        if (ctx.thread->joinable())
            return false;

        ctx.active.wait(false);
        return true;
    }

    /**
     * Checks if a network connection is established
     * @return true if connected
     */
    [[nodiscard]] bool isConnected() const { return ctx.con_opened; }

protected:
    void kill() { ctx.quit = true; }
    context ctx;
};

/**
 * Server implementation of NetworkThread
 * Handles incoming client connections and file synchronization requests
 */
class ServerThread : public NetworkThread
{
public:
    /**
     * Constructs a server thread with the given options
     * @param opts Program configuration options
     */
    ServerThread(const ProgramOptions &opts) : NetworkThread(runserver, opts) {}
private:
    /**
     * Main server loop implementation
     * @param ctx Server context containing configuration and state
     */
    static void runserver(context &ctx);
};

/**
 * Client implementation of NetworkThread
 * Handles connecting to server and initiating file synchronization
 */
class ClientThread : public NetworkThread
{
public:
    /**
     * Constructs a client thread with the given options
     * @param opts Program configuration options
     */
    ClientThread(const ProgramOptions &opts) : NetworkThread(runclient, opts) {}
private:
    /**
     * Main client loop implementation
     * @param ctx Client context containing configuration and state
     */
    static void runclient(context &ctx);

    /**
     * Requests directory index from the server
     * @param options Map containing connection options
     * @return 0 on success, negative value on error
     */
    static int requestIndexFromServer(const std::map<std::string, std::string>& options);
};

#endif // _NETWORK_THREAD_H_