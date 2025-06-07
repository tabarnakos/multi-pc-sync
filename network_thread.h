#ifndef _NETWORK_THREAD_H_
#define _NETWORK_THREAD_H_

#include <functional>
#include <latch>
#include <thread>
#include <map>
#include "program_options.h"

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
	
	NetworkThread( std::function<void(context &)> f, const ProgramOptions &opts ) : 
	ctx(opts)
	{ ctx.thread = new std::thread(std::ref(f), std::ref(ctx)); }
	virtual ~NetworkThread()
	{ kill(); ctx.active.wait(true); ctx.thread->join(); delete ctx.thread; }

	void start() { ctx.latch.count_down(); }
	bool isActive() const { return ctx.active.load(); }
	bool isConnected() const { return ctx.con_opened; }

private:
	void kill() { ctx.quit=true; }
	context ctx;

};

class ServerThread : public NetworkThread
{
public:
	ServerThread(const ProgramOptions &opts) :
	NetworkThread(std::ref(f), opts) {}
private:
	static void runserver(context & ctx);
	const std::function<void(context &)> f = runserver;
};

class ClientThread : public NetworkThread
{
public:
	ClientThread(const ProgramOptions &opts) : 
	NetworkThread(std::ref(f), opts) {}
private:
	static void runclient(context & ctx);
	static int requestIndexFromServer(const std::map<std::string, std::string>& options);
	const std::function<void(context &)> f = runclient;
};

#endif //_NETWORK_THREAD_H_