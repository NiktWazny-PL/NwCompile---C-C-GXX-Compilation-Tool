#pragma once

#include "Yaml.hpp"

#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>
#include <format>

namespace Nw {

	struct Error
	{
		static Error FileNotFound(const std::filesystem::path& path, int64_t code = 1)
		{
			Error err;
			err.Message = std::format("Couldn't find file: \"{}\"", path.string());
			err.Code    = code;

			return err;
		}
		static Error NotAFile(const std::filesystem::path& path, int64_t code = 2)
		{
			Error err;
			err.Message = std::format("This path doesn't lead to a file: \"{}\"", path.string());
			err.Code    = code;

			return err;
		}

		std::string Message;
		int64_t     Code;
	};

	class Tool
	{
	public:
		using StringArray       = std::vector<std::string>;
		using Path              = std::filesystem::path;
		using PathArray         = std::vector<Path>;
		using Gxx               = std::string;
		using Standard          = std::string;
		using ProjectName       = std::string;
		using String            = std::string;
		using OptimalizationLvl = uint8_t;

		enum class ProjectType : int8_t
		{
			Invalid = -1,
			Normal  =  0,
			DynamicLib,
			StaticLib, 
		};

		struct Configuration
		{
			String            Name;
			StringArray       Defines;
			PathArray         Includes;
			PathArray         Links;
			StringArray       CompilerFlags;
			StringArray       LinkerFlags;
			OptimalizationLvl OptimLvl;
		};

		struct Project
		{
			ProjectName   Name;
			ProjectType   Type;
			Gxx           GxxVersion;
			Standard      CppStandard;
			Path          Cwd;
			Path          PrcFile;
			Path          HdrDir;
			Path          SrcDir;
			Path          BinDir;
			StringArray   Defines;
			PathArray     IncludeDirs;
			PathArray     LinkFiles;
			StringArray   CompilerFlags;
			StringArray   LinkerFlags;
			Configuration Config;
		};

	public:
		Tool() = default;

		void Run(const Path& projectsPath);

	private:
		void ILoadProjects(const Path& projectsPath);
		void ICreateDirectories();

		static void IPrecompile(Project& proj);
		static void ICompileIntermediate(Project& proj);
		static void ILink(Project& proj);

	private:
		std::vector<Project> m_Projects;
	};

}