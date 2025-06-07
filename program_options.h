#ifndef _PROGRAM_OPTIONS_H_
#define _PROGRAM_OPTIONS_H_

#include <filesystem>

class ProgramOptions
{
public:
	enum MODE
	{
		MODE_CLIENT = 0,
		MODE_SERVER,
	};
	const std::filesystem::path path;
	std::string ip;
	int port;
	MODE mode;

	static ProgramOptions parseArgs(int argc, char *argv[]);
	
private:
	ProgramOptions(int argc, char *argv[]) :
	path( argv[argc-1] )
	{}
};

#endif  //_PROGRAM_OPTIONS_H_