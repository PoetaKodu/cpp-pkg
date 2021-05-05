#include PACC_PCH

#include <Pacc/Generators/Premake5.hpp>
#include <Pacc/OutputFormatter.hpp>
#include <Pacc/Filesystem.hpp>
#include <Pacc/Environment.hpp>

using namespace fmt;

template <typename T = std::string_view>
using DictElem 	= std::pair<std::string_view, T>;
template <typename T = std::string_view>
using Dict 		= std::vector< DictElem<T> >;

namespace constants
{

/////////////////////////////////////////////////////////////////////
constexpr std::string_view DefaultPremakeCfg =
R"DefaultCfg(
platforms { "x86", "x64" }
	configurations { "Debug", "Release" }

	location ("build")
	targetdir(path.join(os.getcwd(), "bin/%{cfg.platform}/%{cfg.buildcfg}"))
	
	if os.host() == "macosx" then
		removeplatforms { "x86" }
	end

	filter "platforms:*32"
		architecture "x86"

	filter "platforms:*64"
		architecture "x86_64"

	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"

	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "On"

	filter {}
)DefaultCfg";

namespace mappings
{

/////////////////////////////////////////////////////////////////////
auto const& LangToPremakeLangAndDialect()
{
	using LangAndDialect = std::pair<std::string_view, std::string_view>;
	static const Dict<LangAndDialect> dict = {
		{ "C89", 	{ "C", 		"" } },
		{ "C90", 	{ "C", 		"" } },
		{ "C95", 	{ "C", 		"" } },
		{ "C99", 	{ "C", 		"" } },
		{ "C11", 	{ "C", 		"" } },
		{ "C17", 	{ "C", 		"" } },
		{ "C++98", 	{ "C++", 	"C++98" } },
		{ "C++0x", 	{ "C++", 	"C++11" } },
		{ "C++11", 	{ "C++", 	"C++11" } },
		{ "C++1y", 	{ "C++", 	"C++14" } },
		{ "C++14", 	{ "C++", 	"C++14" } },
		{ "C++1z", 	{ "C++", 	"C++17" } },
		{ "C++17", 	{ "C++", 	"C++17" } }
	};
	return dict;
}

/////////////////////////////////////////////////////////////////////
auto const& AppTypeToPremakeKind()
{
	static const Dict<> AppTypeToPremakeKind = {
		{ "app", 		"ConsoleApp" },
		{ "static lib", "StaticLib" },
		{ "shared lib", "SharedLib" }
	};
	return AppTypeToPremakeKind;
}

}

}


namespace gen
{

/////////////////////////////////////////////////
auto getNumElements(VecOfStr const& v)
{
	return v.size();
}

/////////////////////////////////////////////////
auto getNumElements(VecOfStrAcc const& v)
{
	return v.public_.size() + v.private_.size() + v.interface_.size();
}


/////////////////////////////////////////////////
template <typename T>
auto getAccesses(std::vector<T> const& v)
{
	return std::vector{ &v };
}

/////////////////////////////////////////////////
template <typename T>
auto getAccesses(AccessSplitVec<T> const& v)
{
	return std::vector{ &v.private_, &v.public_, &v.interface_ };
}


/////////////////////////////////////////////////
template <typename T>
auto getAccesses(std::vector<T>& v)
{
	return std::vector<T*>{ &v };
}

/////////////////////////////////////////////////
template <typename T>
auto getAccesses(AccessSplitVec<T>& v)
{
	return std::vector{ &v.private_, &v.public_, &v.interface_ };
}


void appendWorkspace		(OutputFormatter &fmt_, Package const& pkg_);
void appendProject			(OutputFormatter &fmt_, Project const& project_);
template <typename T>
void appendPropWithAccess	(OutputFormatter &fmt_, std::string_view propName, T const& values_);
template <typename T>
void appendStringsWithAccess(OutputFormatter &fmt_, T const& vec_);


/////////////////////////////////////////////////
void Premake5::generate(Package & pkg_)
{
	// Prepare output buffer
	std::string out;
	out.reserve(4 * 1024 * 1024);

	OutputFormatter fmt{out};

	this->loadDependencies(pkg_);
	appendWorkspace(fmt, pkg_);

	// Store the output in the premake file
	std::ofstream("premake5.lua") << out;
}

/////////////////////////////////////////////////
Package loadPackageByName(std::string_view name)
{
	const std::vector<fs::path> candidates = {
		fs::current_path() / "pacc_packages",
		env::getPaccDataStorageFolder() / "packages"
	};

	// Get first matching candidate:
	for(auto const& c : candidates)
	{
		auto pkgFolder = c / name;
		try {
			return Package::load(pkgFolder);
		}
		catch(...)
		{
			// Could not load, ignore
		}
	}

	// Found none.
	throw std::runtime_error(fmt::format("Could not find package \"{}\" (TODO: help here).", name));
}

/////////////////////////////////////////////////
bool Premake5::compareDependency(Dependency const& left_, Dependency const& right_)
{
	if (&left_ == &right_)
		return true;

	if (left_.type() != right_.type())
		return false;

	// Doesn't matter if we use `left_` or `right_`:
	switch(left_.type())
	{
	case Dependency::Raw:
	{
		return left_.raw() == right_.raw();

		break;
	}
	case Dependency::Package:
	{
		// TODO: improve it
		auto const& leftPkg 	= left_.package();
		auto const& rightPkg 	= right_.package();

		if (leftPkg.version 	== rightPkg.version &&
			leftPkg.packageName == rightPkg.packageName)
		{
			return true;
		}

		break;
	}
	}

	return false;
}

/////////////////////////////////////////////////
PackagePtr Premake5::findPackageByRoot(fs::path root_) const
{
	auto it = std::lower_bound(
			loadedPackages.begin(), loadedPackages.end(),
			root_,
			[](auto const& e, fs::path const& inserted) { return e->root < inserted; }
		);

	if (it != loadedPackages.end() && (*it)->root == root_)
		return *it;

	return nullptr;
}


/////////////////////////////////////////////////
bool Premake5::wasPackageLoaded(fs::path root_) const
{
	auto it = std::lower_bound(
			loadedPackages.begin(), loadedPackages.end(),
			root_,
			[](auto const& e, fs::path const& inserted) { return e->root < inserted; }
		);

	if (it != loadedPackages.end() && (*it)->root == root_)
		return true;

	return false;
}

enum class AccessType
{
	Private,
	Public,
	Interface
};

template <typename T>
auto& targetByAccessType(AccessSplit<T> & accessSplit_, AccessType type_)
{
	switch(type_)
	{
	case AccessType::Private: 		return accessSplit_.private_;
	case AccessType::Public: 		return accessSplit_.public_;
	case AccessType::Interface: 	return accessSplit_.interface_;
	}
}

template <typename T, typename TMapValueFn = std::nullptr_t>
void mergeFields(std::vector<T>& into_, std::vector<T> const& from_, TMapValueFn mapValueFn_ = nullptr)
{
	// Do not map values
	if constexpr (std::is_same_v<TMapValueFn, std::nullptr_t>)
	{
		into_.insert(
				into_.end(),
				from_.begin(),
				from_.end()
			);
	}
	else
	{
		into_.reserve(from_.size());
		for(auto const & elem : from_)
		{
			into_.push_back( mapValueFn_(elem));
		}
	}
}

template <typename T, typename TMapValueFn = std::nullptr_t>
void mergeAccesses(T &into_, T const & from_, AccessType method_, TMapValueFn mapValueFn_ = nullptr)
{
	
	auto& target = targetByAccessType(into_.computed, method_); // by default

	// Private is private
	// Merge only interface and public:
	auto forBoth =
		[](auto & selfAndComputed, auto const& whatToDo)
		{
			whatToDo(selfAndComputed.computed);
			whatToDo(selfAndComputed.self);
		};

	auto mergeFieldsTarget =
		[&](auto &selfOrComputed)
		{
			mergeFields(target, selfOrComputed.interface_, mapValueFn_);
			mergeFields(target, selfOrComputed.public_, mapValueFn_);
		};

	forBoth(from_, mergeFieldsTarget);
}

/////////////////////////////////////////////////
void Premake5::loadDependencies(Package & pkg_)
{
	const std::array<AccessType, 3> methodsLoop = {
			AccessType::Private,
			AccessType::Public,
			AccessType::Interface
		};
	size_t methodIdx = 0;

	for(auto & p : pkg_.projects)
	{
		methodIdx = 0;
		for(auto* access : getAccesses(p.dependencies.self))
		{
			for(auto& dep : *access)
			{
				switch(dep.type())
				{
				case Dependency::Raw:
				{
					auto& rawDep = dep.raw();
					// fmt::print("Added raw dependency \"{}\"\n", rawDep);

					auto& target = targetByAccessType(p.linkedLibraries.computed, methodsLoop[methodIdx]);
					// TODO: improve this:
					target.push_back( rawDep );
					break;
				}
				case Dependency::Package:
				{
					auto& pkgDep = dep.package();

					PackagePtr pkgPtr;

					// TODO: load dependency (and bind it to shared pointer)
					{
						Package pkg = loadPackageByName(pkgDep.packageName);

						if (this->wasPackageLoaded(pkg.root))
							continue; // ignore package, was loaded yet

						if (pkgDep.version.empty())
							fmt::print("Loaded dependency \"{}\"\n", pkgDep.packageName);
						else
							fmt::print("Loaded dependency \"{}\"@\"{}\"\n", pkgDep.packageName, pkgDep.version);
				
						pkgPtr = std::make_shared<Package>(std::move(pkg));
					}

					// Assign loaded package:
					pkgDep.package = pkgPtr;

					// Insert in sorted order:
					{
						auto it = std::upper_bound(
								loadedPackages.begin(), loadedPackages.end(),
								pkgPtr->root,
								[](fs::path const& inserted, auto const& e) { return e->root < inserted; }
							);

						loadedPackages.insert(it, pkgPtr);
					}

					configQueue.push_back(dep);
					this->loadDependencies(*pkgPtr);

					auto resolvePath = [&](auto const& pathElem) {
							fs::path path = fs::u8path(pathElem);
							if (path.is_relative())
								return fsx::fwd(pkgPtr->root.parent_path() / path).string();
							else 
								return pathElem;
						};
					for (auto const & depProjName : pkgDep.projects)
					{
						Project const* remoteProj = pkgPtr->findProject(depProjName);

						mergeAccesses(p.defines, 			remoteProj->defines, 			methodsLoop[methodIdx]);
						mergeAccesses(p.includeFolders, 	remoteProj->includeFolders, 	methodsLoop[methodIdx], resolvePath);
						mergeAccesses(p.linkerFolders, 		remoteProj->linkerFolders, 		methodsLoop[methodIdx], resolvePath);
						mergeAccesses(p.linkedLibraries, 	remoteProj->linkedLibraries, 	methodsLoop[methodIdx]);
					}

					
					break;
				}
				}
			}
			
			methodIdx++;
		}
	}
}

/////////////////////////////////////////////////
void appendWorkspace(OutputFormatter &fmt_, Package const& pkg_)
{
	fmt_.write("workspace(\"{}\")\n", pkg_.name);

	{
		IndentScope indent{fmt_};

		// TODO: manual configuration
		fmt_.writeRaw(constants::DefaultPremakeCfg);
		
		for(auto const& project : pkg_.projects)
		{
			appendProject(fmt_, project);
		}
	}
}

/////////////////////////////////////////////////
bool compareIgnoreCase(std::string_view l, std::string_view r)
{
	if (l.length() != r.length()) return false;

	for(std::size_t i = 0; i < l.size(); i++)
	{
		if ( std::tolower(int(l[i])) != std::tolower(int(r[i])) )
			return false;
	}

	return true;
}


/////////////////////////////////////////////////
template <typename T>
auto mapString(Dict<T> const& dict_, std::string_view v)
{
	for(auto it = dict_.begin(); it != dict_.end(); it++)
	{
		if (compareIgnoreCase(std::get<0>(*it), v))
			return it;
	}
	return dict_.end();
}

/////////////////////////////////////////////////
std::string_view mapToPremake5Kind(std::string_view projectType_)
{
	auto const& AppTypeMapping = constants::mappings::AppTypeToPremakeKind();

	auto it = mapString(AppTypeMapping, projectType_);
	if (it != AppTypeMapping.end())
		return it->second;
	
	return "";
}

/////////////////////////////////////////////////
void appendPremake5Lang(OutputFormatter& fmt_, std::string_view lang_)
{
	auto const& LangMapping = constants::mappings::LangToPremakeLangAndDialect();

	auto it = mapString(LangMapping, lang_);
	if (it != LangMapping.end())
	{
		auto const& premakeVal = it->second;
		
		fmt_.write("language (\"{}\")\n", premakeVal.first);
		if (!premakeVal.second.empty())
			fmt_.write("cppdialect (\"{}\")\n", premakeVal.second);
	}
}



/////////////////////////////////////////////////
void appendProject(OutputFormatter &fmt_, Project const& project_)
{
	fmt_.write("\n");
	fmt_.write("project(\"{}\")\n", project_.name);

	// Format project settings:
	{
		IndentScope indent{fmt_};

		// TODO: value mapping (enums, etc)
		fmt_.write("kind(\"{}\")\n", mapToPremake5Kind(project_.type));

		// TODO: extract this to functions
		// TODO: merge language and c/cpp dialect into one
		if (!project_.language.empty())
			appendPremake5Lang(fmt_, project_.language);
		else {
			// TODO: use configuration file to get default values
			fmt_.write("language(\"C++\")\n");
			fmt_.write("cppdialect(\"C++17\")\n");
		}

		// TODO: Refactor this code

		// Computed:
		appendPropWithAccess(fmt_, "defines", 		project_.defines.computed);
		appendPropWithAccess(fmt_, "links", 		project_.linkedLibraries.computed);
		appendPropWithAccess(fmt_, "includedirs", 	project_.includeFolders.computed);
		appendPropWithAccess(fmt_, "libdirs", 		project_.linkerFolders.computed);

		
		appendPropWithAccess(fmt_, "files", 		project_.files);
		appendPropWithAccess(fmt_, "defines", 		project_.defines.self);
		appendPropWithAccess(fmt_, "links", 		project_.linkedLibraries.self);
		appendPropWithAccess(fmt_, "includedirs", 	project_.includeFolders.self);
		appendPropWithAccess(fmt_, "libdirs", 		project_.linkerFolders.self);

	}
}



/////////////////////////////////////////////////
template <typename T>
void appendPropWithAccess(OutputFormatter &fmt_, std::string_view propName, T const& values_)
{
	if (getNumElements(values_) > 0)
	{
		fmt_.write("{} ({{\n", propName);
		{
			IndentScope indent{fmt_};
			appendStringsWithAccess(fmt_, values_);
		}
		fmt_.write("}})\n");
	}
}



/////////////////////////////////////////////////
template <typename T>
void appendStringsWithAccess(OutputFormatter &fmt_, T const& acc_)
{
	for(auto const* acc : getAccesses(acc_))
	{
		if (acc->size() > 0)
		{
			appendStrings(fmt_, *acc);
			fmt_.write("\n");
		}
	}
}

/////////////////////////////////////////////////
void appendStrings(OutputFormatter &fmt_, VecOfStr const& vec_)
{
	for(auto const & str : vec_)
		fmt_.write("\"{}\",\n", str);
}


}