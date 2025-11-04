#include <iostream>
#include <string>
#include <Windows.h>

// Project headers
#include "overlay/load.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {

	// Render loop
	load();

}