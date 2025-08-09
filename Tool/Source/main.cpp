#include "Tool.hpp"

#include <iostream>

namespace Nw {

	int EntryPnt(int numArgs, char** argv)
	{
		std::filesystem::path fileWithProjects;

		if (numArgs < 2)
		{
			std::cout << "Give me the path to the file file with projects to compile:\n> ";
			std::cin >> fileWithProjects;
		}
		else
			fileWithProjects = argv[2];
		
		Tool tool;
		tool.Run(fileWithProjects);

		return 0;
	}

}

#if _WIN32

#include <Windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return Nw::EntryPnt(__argc + 1, __argv - 1);
}

#else

int main(int argc, char** argv)
{
	return Nw::EntryPnt(argc + 1, argv - 1);
}

#endif