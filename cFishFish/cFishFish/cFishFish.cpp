// cFishFish.cpp : Defines the entry point for the application.
//

#include "cFishFish.h"
#include "Engine/Eval.h"
#include "Engine/UCI.h"

int main()
{

	UCI uci;

	Board* board = new Board;

	while (true) {

		std::string command;
		std::getline(std::cin, command);

		uci.parse(command);

	}
	return 0;
}
