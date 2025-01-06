// cFishFish.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>

#include <iostream>
#include "Chess.h"
#include "Eval.h"
#include "UCI.h"

using namespace chess;

int main() {

	UCI* uci = new UCI;

	while (true) {

		std::string command;
		std::getline(std::cin, command);

		uci->parse(command);

	}
	return 0;
}
