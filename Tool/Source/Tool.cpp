#include "Tool.hpp"

#include <thread>
#include <mutex>
#include <future>
#include <expected>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <fcntl.h>
#include <io.h>

namespace Utils {

	static std::ofstream s_LogFile("Compile.log");
	static std::mutex    s_PrintlnMtx;
	
	class StderrCapture
	{
	public:
		StderrCapture()
		{
			if (_pipe(m_PipeFds, 1024, O_TEXT) != 0)
				return;

			m_OldStderrFd = _dup(_fileno(stderr));
			if (m_OldStderrFd == -1)
				return;

			if (_dup2(m_PipeFds[1], _fileno(stderr)) == -1)
				return;

			_close(m_PipeFds[1]);
		}

		~StderrCapture()
		{
			Stop();
		}

		void Stop()
		{
			if (!m_Active)
				return;

			fflush(stderr);
			_dup2(m_OldStderrFd, _fileno(stderr));
			_close(m_OldStderrFd);

			m_Active = false;
		}

		std::string GetString()
		{
			Stop();

			std::ostringstream oss;
			char buffer[256];

			int bytesRead;
			while ((bytesRead = _read(m_PipeFds[0], buffer, sizeof(buffer) - 1)) > 0)
			{
				buffer[bytesRead] = '\0';
				oss << buffer;
			}

			_close(m_PipeFds[0]);
			return oss.str();
		}

	private:
		int m_PipeFds[2] {};
		int m_OldStderrFd = -1;
		bool m_Active = true;
	};

	namespace Parallel {

		template<typename T, typename Fn>
		void For(T beginIdx, T endIdx, Fn func)
		{
			if (endIdx <= beginIdx)
				return;

			const uint32_t c_Total      = std::abs(beginIdx - endIdx);
			const uint32_t c_NumThreads = std::thread::hardware_concurrency();
			
			const uint32_t c_ChunkSize  = (c_Total + c_NumThreads - 1) / c_NumThreads;

			std::vector<std::thread> threadPool;

			for (uint32_t i = 0; i < c_NumThreads; i++)
			{
				const T c_ChunkBegin = beginIdx + i * c_ChunkSize;
				const T c_ChunkEnd   = std::min(beginIdx + (i+1) * c_ChunkSize, endIdx);

				if (c_ChunkBegin >= c_ChunkEnd)
					continue;

				threadPool.emplace_back([c_ChunkBegin, c_ChunkEnd, func]()
				{
					for (T i = c_ChunkBegin; i < c_ChunkEnd; i++)
						func(i);
				});
			}

			for (auto& thread : threadPool)
				thread.join();
		}

	}

	// I'm not using std::println because it throws an error during linking for some reason

	template<typename... Args>
	void Println(std::format_string<Args...> fmt, Args&&... args)
	{
		const std::string c_Message = std::format(fmt, std::forward<Args>(args)...);

		{
			std::unique_lock<std::mutex> lock(s_PrintlnMtx);

			std::cout << c_Message << '\n';
			s_LogFile << c_Message << '\n';
		}
	}

	template<typename... Args>
	std::optional<Nw::Error> RunCommand(std::format_string<Args...> fmt, Args&&... args)
	{
		const std::string c_Command = std::format(fmt, std::forward<Args>(args)...);

		Println("|  |> {}", c_Command);

		StderrCapture capture;
		const int c_Code = system(c_Command.c_str());

		if (c_Code != 0)
		{
			Nw::Error err;
			err.Message = capture.GetString();
			err.Code    = c_Code;
			
			return err;
		}

		return {};
	}

	template<typename T>
	std::vector<T> LoadVector(Yaml::Node& node)
	{
		std::vector<T> result;

		if (node.IsSequence())
		{
			result.reserve(node.Size());

			for (Yaml::Iterator iter = node.Begin(); iter != node.End(); iter++)
			{
				if constexpr (std::is_same_v<T, std::pair<std::string, Yaml::Node>>)
					result.push_back(*iter);
				else
					result.push_back((*iter).second.As<T>());
			}
		}

		return result;
	}

	template<typename T>
	std::string CollapseList(const std::vector<T>& list, std::string_view pre, std::string_view spacer = " ")
	{
		if (list.empty())
			return "";

		std::stringstream ss;
		for (const T& item : list)
			ss << pre << item << spacer;

		return ss.str();
	}

	std::expected<size_t, Nw::Error> NumFilesInDirectory(const std::filesystem::path& path)
	{
		size_t num = 0;

		if (!std::filesystem::is_directory(path))
			return std::unexpected(Nw::Error("Not a directory: " + path.string(), 1));

		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path))
		{
			if (entry.is_directory())
				continue;

			num++;
		}

		return num;
	}

	Nw::Tool::ProjectType TypeFromString(std::string_view str)
	{
		using ProjTp = Nw::Tool::ProjectType;

		if (str == "Normal")
			return ProjTp::Normal;
		else if (str == "DynamicLib")
			return ProjTp::DynamicLib;
		else if (str == "StaticLib")
			return ProjTp::StaticLib;

		return ProjTp::Invalid;
	}

	Nw::Tool::Configuration LoadConfig(Yaml::Node& node, std::string_view name)
	{
		Nw::Tool::Configuration cfg;
		cfg.Name          = name;
		cfg.Defines       = LoadVector<std::string>(node["Defines"]);
		cfg.Includes      = LoadVector<std::filesystem::path>(node["Defines"]);
		cfg.CompilerFlags = LoadVector<std::string>(node["CompilerFlags"]);
		cfg.LinkerFlags   = LoadVector<std::string>(node["CompilerFlags"]);
		cfg.OptimLvl      = node["OptimLvl"].As<int>();

		return std::move(cfg);
	}

	Nw::Tool::Project LoadProject(Yaml::Node& node, std::string_view name)
	{
		Nw::Tool::Project proj;
		proj.Name          = name;
		proj.Type          = TypeFromString(node["ProjectType"].As<std::string>());
		proj.GxxVersion    = node["Gxx"].As<std::string>("g++");
		proj.CppStandard   = node["Standard"].As<std::string>("c++23");
		proj.Cwd           = node["Cwd"].As<std::string>(std::filesystem::current_path().string());
		proj.HdrDir        = proj.Cwd / node["HeaderDir"].As<std::string>("Header");
		proj.SrcDir        = proj.Cwd / node["SourceDir"].As<std::string>("Source");
		proj.BinDir        = proj.Cwd / node["BinaryDir"].As<std::string>("Binary");
		proj.PrcFile       = proj.HdrDir / node["Prc"].As<std::string>("null");
		proj.Defines       = LoadVector<std::string>(node["GlobalDefines"]);
		proj.IncludeDirs   = LoadVector<std::filesystem::path>(node["GlobalIncludeDirs"]);
		proj.LinkFiles     = LoadVector<std::filesystem::path>(node["GlobalIncludeDirs"]);;
		proj.CompilerFlags = LoadVector<std::string>(node["GlobalCompilerFlags"]);
		proj.LinkerFlags   = LoadVector<std::string>(node["GlobalLinkerFlags"]);
		proj.Config        = LoadConfig(node["Configurations"]["CurrentConfigName"], node["CurrentConfigName"].As<std::string>());

		proj.IncludeDirs.push_back(proj.HdrDir);

		return std::move(proj);
	}

	std::string ExtentionFromType(Nw::Tool::ProjectType type)
	{
		using ProjTp = Nw::Tool::ProjectType;

		switch (type)
		{
			case (ProjTp::Normal):     return ".exe";
			case (ProjTp::DynamicLib): return ".dll";
			case (ProjTp::StaticLib):  return ".lib";
		}

		return "";
	}

}

namespace Nw {

	void Tool::Run(const Path& projectsPath)
	{
		Utils::RunCommand("cls");
		Utils::Println("|>-------------------------------------------------------------");
		Utils::Println("|> NwCompile C/C++ GCC Code Compilation tool. Version: 0.0.1");
		Utils::Println("|");

		ILoadProjects(projectsPath);
		ICreateDirectories();

		for (Project& proj : m_Projects)
		{
			Utils::Println("|> Project -- {}", proj.Name);
			{
				if (proj.PrcFile != (proj.HdrDir / "null"))
					IPrecompile(proj);
				
				const std::expected<size_t, Error> c_MaybeNumSrcFiles = Utils::NumFilesInDirectory(proj.SrcDir);
				if (c_MaybeNumSrcFiles)
				{
					if (*c_MaybeNumSrcFiles > 0)
						ICompileIntermediate(proj);
				}
				else
				{
					const Error c_Err = c_MaybeNumSrcFiles.error();

					Utils::Println("|> Compiling - Error found:");
					Utils::Println("|  |-Msg:  {}", c_Err.Message);
					Utils::Println("|  |-Code: {}", c_Err.Code);
					Utils::Println("|");

					exit(1);
				}

				const std::expected<size_t, Error> c_MaybeNumBinFiles = Utils::NumFilesInDirectory(proj.BinDir);
				if (c_MaybeNumBinFiles)
				{
					if (*c_MaybeNumBinFiles > 0)
					{
						Utils::Println("|> Linking...");

						ILink(proj);
					}
				}
				else
				{
					const Error& c_Err = c_MaybeNumBinFiles.error();

					Utils::Println("|> Linking - Error found:");
					Utils::Println("|  |-Msg:  {}", c_Err.Message);
					Utils::Println("|  |-Code: {}", c_Err.Code);
					Utils::Println("|");

					exit(1);
				}
			}
		}

		Utils::Println("|>-------------------------------------------------------------");
	}

	void Tool::ILoadProjects(const Path& projectsPath)
	{
		Yaml::Node root;
		{
			const std::string c_SrcPath = projectsPath.string();

			Yaml::Parse(root, c_SrcPath.c_str());
		}

		m_Projects.reserve(root.Size());
		for(Yaml::Iterator iter = root.Begin(); iter != root.End(); iter++)
		{
			const std::string& c_Name = (*iter).first;
			Yaml::Node&        node   = (*iter).second;

			m_Projects.emplace_back(Utils::LoadProject(node, c_Name));
		};
	}

	void Tool::ICreateDirectories()
	{
		for (Project& proj : m_Projects)
		{
			std::filesystem::create_directories(proj.Cwd);
			std::filesystem::create_directories(proj.HdrDir);
			std::filesystem::create_directories(proj.SrcDir);
			std::filesystem::create_directories(proj.BinDir);
			std::filesystem::create_directories(proj.BinDir / proj.Config.Name);
		}
	}

	void Tool::IPrecompile(Project& proj)
	{
		Utils::RunCommand("{} -c \"{}\"", proj.GxxVersion, proj.PrcFile.string());		
	}

	void Tool::ICompileIntermediate(Project& proj)
	{
		const size_t c_NumSrcFiles = *Utils::NumFilesInDirectory(proj.SrcDir);

		std::vector<std::future<void>> asyncProcesses;
		asyncProcesses.reserve(c_NumSrcFiles);

		std::vector<std::optional<Error>> errs;
		errs.resize(c_NumSrcFiles);

		size_t idx = 0;
		for (std::filesystem::directory_entry entry : std::filesystem::recursive_directory_iterator(proj.SrcDir))
		{
			if (entry.is_directory())
				continue;

			asyncProcesses.emplace_back(std::async(std::launch::async,
				[idx, entry, &proj, &errs]() -> void
				{
					Utils::Println("|> Compiling \"{}\"", (proj.SrcDir / entry.path().filename()).string());

					errs[idx] = Utils::RunCommand("{}", Utils::CollapseList<String>({
						proj.GxxVersion,
						"-c",
						(proj.SrcDir / entry.path().filename()).string(),
						"-o",
						(proj.BinDir / proj.Config.Name / entry.path().filename().replace_extension(".obj")).string(),
						std::format("-O{}", proj.Config.OptimLvl),
						std::format("-std={}", proj.CppStandard),
						Utils::CollapseList(proj.Defines,       "-D"),
						Utils::CollapseList(proj.IncludeDirs,   "-I"),
						Utils::CollapseList(proj.CompilerFlags, ""),
						Utils::CollapseList(proj.Config.Defines,       "-D"),
						Utils::CollapseList(proj.Config.Includes,      "-I"),
						Utils::CollapseList(proj.Config.CompilerFlags, ""),
					}, ""));
				}
			));

			idx++;
		}

		for (std::future<void>& proc : asyncProcesses)
			proc.get();

		for (const std::optional<Error>& err : errs)
		{
			if (!err)
				continue;

			Utils::Println("|> Compile - Error found:");
			Utils::Println("|  |-Msg:  {}", err->Message);
			Utils::Println("|  |-Code: {}", err->Code);
		}
	}

	void Tool::ILink(Project& proj)
	{
		std::optional<Error> maybeError = std::nullopt;

		switch (proj.Type)
		{
			case (ProjectType::Normal):
			{
				maybeError = Utils::RunCommand("{}", Utils::CollapseList<String>({
					proj.GxxVersion,
					(proj.BinDir / proj.Config.Name / "*").replace_extension(".obj").string(),
					"-o",
					(proj.Cwd / proj.Name).replace_extension(".exe").string(),
					Utils::CollapseList(proj.LinkFiles,   "-l"),
					Utils::CollapseList(proj.LinkerFlags, ""),
					Utils::CollapseList(proj.Config.Links,       "-l"),
					Utils::CollapseList(proj.Config.LinkerFlags, ""),
				}, ""));

				break;
			}

			case (ProjectType::DynamicLib):
			{
				auto dllPathStr = (proj.Cwd / proj.Name).replace_extension(".dll").string();
				auto importLibPathStr = (proj.BinDir / ("lib" + proj.Name)).replace_extension(".a").string();

				maybeError = Utils::RunCommand("{}", Utils::CollapseList<String>({
					proj.GxxVersion,
					"-shared",
					(proj.BinDir / proj.Config.Name / "*").replace_extension(".obj").string(),
					"-o",
					dllPathStr,
					std::format("-Wl,--out-implib,{}", importLibPathStr),
					Utils::CollapseList(proj.LinkFiles,   "-l"),
					Utils::CollapseList(proj.LinkerFlags, ""),
					Utils::CollapseList(proj.Config.Links,       "-l"),
					Utils::CollapseList(proj.Config.LinkerFlags, ""),
				}, ""));

				break;
			}

			case (ProjectType::StaticLib):
			{
				auto staticLibPathStr = (proj.BinDir / proj.Config.Name / ("lib" + proj.Name)).replace_extension(".a").string();

				maybeError = Utils::RunCommand("{}", Utils::CollapseList<String>({
					"ar",
					"rcs",
					staticLibPathStr,
					(proj.BinDir / proj.Config.Name / "*").replace_extension(".obj").string()
				}, ""));

				break;
			}
		}

		if (maybeError)
		{
			Utils::Println("|> Linking - Error found:");
			Utils::Println("|    Msg:  {}", maybeError->Message);
			Utils::Println("|    Code: {}", maybeError->Code);
			Utils::Println("|");
		}
	}

}
