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

// Project Includes
#include "program_options.h"

// Section 2: Class Definitions
class NetworkThread
{
public:
    struct context
    {
        context(const ProgramOptions &opts) :
            opts(opts),
            latch(1),
            thread(nullptr),
            quit(false),
            active(false),
            con_opened(false)
        {}
        ~context() = default;

        const ProgramOptions opts;
        std::latch latch;
        std::thread *thread;
        std::atomic<bool> quit;
        std::atomic<bool> active;
        bool con_opened;
    };

    NetworkThread(const std::function<void(context &)> &f, const ProgramOptions &opts) : ctx(opts)
    { ctx.thread = new std::thread(f, std::ref(ctx)); }
    virtual ~NetworkThread()
    { kill(); ctx.active.wait(true); ctx.thread->join(); delete ctx.thread; }

    void start() { ctx.latch.count_down(); }
    bool isActive() const { return ctx.active.load(); }
    bool isConnected() const { return ctx.con_opened; }

protected:
    void kill() { ctx.quit = true; }
    context ctx;
};

class ServerThread : public NetworkThread
{
public:
    ServerThread(const ProgramOptions &opts) : NetworkThread(runserver, opts) {}
private:
    static void runserver(context &ctx);
};

class ClientThread : public NetworkThread
{
public:
    ClientThread(const ProgramOptions &opts) : NetworkThread(runclient, opts) {}
private:
    static void runclient(context &ctx);
    static int requestIndexFromServer(const std::map<std::string, std::string>& options);
};

#endif // _NETWORK_THREAD_H_