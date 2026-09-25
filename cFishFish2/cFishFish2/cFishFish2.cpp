// cFishFish2.cpp : Defines the entry point for the application.
//

#include "cFishFish2.h"
#include "uci.h"

#include <string>

int main(int argc, char* argv[])
{
	uci::Uci engine;

	// Command-line mode: arguments are run as one command, then the program
	// exits. E.g. "cFishFish2 bench", "cFishFish2 bench 8",
	// "cFishFish2 testsuite wac.epd 1000".
	if (argc > 1) {
		std::string cmd = argv[1];
		for (int i = 2; i < argc; ++i) cmd += std::string(" ") + argv[i];
		engine.handle(cmd);
		return 0;
	}

	engine.loop();
	return 0;
}
