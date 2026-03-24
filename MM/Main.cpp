#include "stdafx.h"

#include "DemoRunner.h"

int main()
{
	std::srand(static_cast<unsigned int>(::time(nullptr)));
	return RunDemo();
}
