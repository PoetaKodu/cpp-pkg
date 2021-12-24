#include PACC_PCH

#include <Pacc/PackageSystem/Package.hpp>
#include <Pacc/App/Errors.hpp>
#include <Pacc/System/Environment.hpp>
#include <Pacc/Readers/General.hpp>
#include <Pacc/System/Filesystem.hpp>
#include <Pacc/Readers/JsonReader.hpp>
#include <Pacc/Generation/BuildQueueBuilder.hpp>


///////////////////////////////////////////////////
// Private functions (forward declaration)
///////////////////////////////////////////////////

template <json::value_t type>
json const* expect(json const& j);

template <json::value_t type>
json const* expectSub(json const& j, std::string_view subfieldName);

template <json::value_t type>
json const& require(json const& j);

template <json::value_t type>
json const& requireSub(json const& j, std::string_view subfieldName);

json const* 	selfOrSubfieldOpt(json const& self, std::string_view fieldName = "");
json const& 	selfOrSubfieldReq(json const& self, std::string_view fieldName = "");
json const* 	selfOrSubfield(json const& self, std::string_view fieldName, bool required = false);

void 			readDependencyAccess(Package &pkg_, Project & proj_, json const& deps_, std::vector<Dependency> &target_);
VecOfStr 		loadVecOfStrField(json const& j, std::string_view fieldName, bool direct = false, bool required = false);
VecOfStrAcc 	loadVecOfStrAccField(json const& j, std::string_view fieldName, AccessType defaultAccess_ = AccessType::Private);


///////////////////////////////////////////////////
// Public functions
///////////////////////////////////////////////////

///////////////////////////////////////////////////
void TargetBase::inheritConfigurationFrom(Package const& fromPkg_, Project const& fromProject_, AccessType mode_)
{
	computeConfiguration( *this, fromPkg_, fromProject_, mode_ );

	// Inherit all premake filters:
	for(auto it : fromProject_.premakeFilters)
	{
		// Ensure configuration exists:
		if (premakeFilters.find(it.first) == premakeFilters.end())
			premakeFilters[it.first] = {};

		// Merge configuration:
		computeConfiguration( premakeFilters.at(it.first), fromPkg_, fromProject_, it.second, mode_ );
	}
}

///////////////////////////////////////////////////
Project::Type Project::parseType(std::string_view type_)
{
	if (compareIgnoreCase(type_, "app"))
		return Project::Type::App;
	else if (compareIgnoreCase(type_, "static lib"))
		return Project::Type::StaticLib;
	else if (compareIgnoreCase(type_, "shared lib"))
		return Project::Type::SharedLib;
	else if (compareIgnoreCase(type_, "interface"))
		return Project::Type::Interface;

	return Project::Type::Unknown;
}

///////////////////////////////////////////////////
PackagePreloadInfo Package::preload(fs::path dir_)
{
	PackagePreloadInfo result;

	if (dir_.empty()) {
		dir_ = fs::current_path();
	}

	auto tryUseFile = [&dir_](fs::path& out_, std::string_view name_)
		{
			fs::path conf = dir_ / name_;
			if (fs::exists(conf)) {
				out_ = std::move(conf);
				return true;
			}
			return false;
		};

	if (!tryUseFile(result.root, PackageLUA) &&
		!tryUseFile(result.root, PackageJSON))
	{
		throw PaccException(errors::NoPackageSourceFile[0])
				.withHelp(errors::NoPackageSourceFile[1]);
	}

	tryUseFile(result.scriptFile, PackageLUAScript);

	return result;
}


///////////////////////////////////////////////////
UPtr<Package> Package::load(PackagePreloadInfo preloadInfo_)
{
	UPtr<Package> pkg;

	// Decide what to do:
	if (preloadInfo_.usesJsonConfig())
	{
		pkg = std::make_unique<Package>();
		pkg->root		= std::move(preloadInfo_.root);
		pkg->scriptFile	= std::move(preloadInfo_.scriptFile);

		Package::loadFromJSON(*pkg, readFileContents(pkg->root));
	}
	else // Lua config
	{
		// TODO: implement this.
		std::cout << "This function is not implemented yet." << std::endl;
	}
	return pkg;
}

/////////////////////////////////////////////////
UPtr<Package> Package::loadByName(std::string_view name_, VersionRequirement verReq_, UPtr<Package>* invalidVersion_)
{
	std::vector<fs::path> candidates = {
			fs::current_path() 					/ "pacc_packages",
			fs::current_path() 					/ "..",
			env::getPaccDataStorageFolder() 	/ "packages"
		};

	// Get first matching candidate:
	for(auto const& c : candidates)
	{
		auto pkgFolder = c / name_;
		UPtr<Package> pkg;
		try {
			pkg = Package::load(pkgFolder);
		}
		catch(...) {
			// Could not load, ignore
			continue;
		}

		if (verReq_.test(pkg->version))
			return pkg;
		else
		{
			if (invalidVersion_)
				*invalidVersion_ = std::move(pkg);
		}
	}

	// (TODO: help here)
	// Found none.
	throw PaccException("Could not find package \"{}\".", name_);
}


///////////////////////////////////////////////////
Project const* Package::findProject(std::string_view name_) const
{
	auto it = rg::find(projects, name_, &Project::name);

	if (it != projects.end())
		return &(*it);

	return nullptr;
}

///////////////////////////////////////////////////
Project const& Package::requireProject(std::string_view name_) const
{
	Project const *proj = this->findProject(name_);
	if (!proj)
		throw PaccException("Project \"{}\" does not exist in package \"{}\"", name_, name);

	return *proj;
}


///////////////////////////////////////////////////
fs::path Package::predictOutputFolder(Project const& project_) const
{
	// TODO: make it configurable:
	return this->root.parent_path() / "bin/%{cfg.platform}/%{cfg.buildcfg}";
}

///////////////////////////////////////////////////
fs::path Package::predictRealOutputFolder(Project const& project_, BuildSettings settings_) const
{
	std::string folder =  fmt::format("bin/{}/{}",
			settings_.platformName,
			settings_.configName
		);

	return this->root.parent_path() / folder;
}

///////////////////////////////////////////////////
fs::path Package::resolvePath( fs::path const& path_) const
{
	if (path_.is_relative())
		return fsx::fwd(root.parent_path() / path_).string();
	else
		return path_;
}

///////////////////////////////////////////////////
void loadConfigurationFromJSON(Package & pkg_, Project & project_, Configuration& conf_, json const& root_)
{
	using fmt::fg, fmt::color;
	using json_vt = json::value_t;

	JsonView jv{root_};

	conf_.symbolVisibility 		= GNUSymbolVisibility::fromString(jv.stringFieldOr("symbolVisibility", "Default"));
	conf_.moduleDefinitionFile 	= jv.stringFieldOr("moduleDefinitionFile", "");

	bool isInterface = (project_.type == Project::Type::Interface);

	AccessType defaultAccess = isInterface ? AccessType::Interface : AccessType::Private;

	conf_.files		 			= loadVecOfStrField(root_, "files");
	conf_.defines.self	 		= loadVecOfStrAccField(root_, "defines", 			defaultAccess);
	conf_.includeFolders.self	= loadVecOfStrAccField(root_, "includeFolders",		defaultAccess);
	conf_.linkerFolders.self	= loadVecOfStrAccField(root_, "linkerFolders", 		defaultAccess);
	conf_.compilerOptions.self	= loadVecOfStrAccField(root_, "compilerOptions", 	defaultAccess);
	conf_.linkerOptions.self	= loadVecOfStrAccField(root_, "linkerOptions", 		defaultAccess);

	// Load dependencies:
	auto depsIt = root_.find("dependencies");
	if (depsIt != root_.end())
	{
		auto& deps = depsIt.value();
		auto& projSelfDeps = conf_.dependencies.self;
		if (deps.type() == json_vt::array)
		{
			readDependencyAccess(pkg_, project_, *depsIt, targetByAccessType(projSelfDeps, defaultAccess));
		}
		else if (deps.type() == json_vt::object)
		{
			if (isInterface)
			{
				if (deps.contains("public") || deps.contains("private"))
					fmt::print(fg(color::yellow), "Interface project \"{}\" cannot include public or private dependencies (ignored).", project_.name);
			}
			else
			{
				if (deps.contains("public")) readDependencyAccess(pkg_, project_, deps["public"], projSelfDeps.public_);
				if (deps.contains("private")) readDependencyAccess(pkg_, project_, deps["private"], projSelfDeps.private_);
			}

			if (deps.contains("interface")) readDependencyAccess(pkg_, project_, deps["interface"], projSelfDeps.interface_);
		}
		else
			throw PaccException("Invalid type of \"dependencies\" field (must be an array or an object)");
	}
}

///////////////////////////////////////////////////
bool Package::loadFromJSON(Package& package_, std::string const& packageContent_)
{
	using json_vt = json::value_t;

	// Parse and make conformant:
	json j;
	PackageJsonReader view{ j };

	j = json::parse(packageContent_);
	view.makeConformant();

	// std::ofstream("package.dump.json") << j.dump(1, '\t');

	// Load JSON:
	package_.name 			= j["name"].get<std::string>();
	package_.startupProject = JsonView{j}.stringFieldOr("startupProject", "");
	package_.version 		= Version::fromString( JsonView{j}.stringFieldOr("version", "0") );

	auto projects = j.find("projects");

	package_.projects.reserve(projects->size());

	// Read projects:
	for(auto it : projects->items())
	{
		auto& jsonProject = it.value();

		Project project;

		project.name 					= jsonProject["name"].get<std::string>();
		project.type 					= Project::parseType(jsonProject["type"].get<std::string>());

		if (auto it = jsonProject.find("pch"); it != jsonProject.end())
		{
			PrecompiledHeader pch;
			// TODO: add validation
			pch.header		= jsonProject["pch"]["header"];
			pch.source		= jsonProject["pch"]["source"];
			pch.definition 	= jsonProject["pch"]["definition"];
			project.pch = std::move(pch);
		}

		// TODO: type and value validation
		if (auto it = jsonProject.find("language"); it != jsonProject.end())
			project.language = it->get<std::string>();

		loadConfigurationFromJSON(package_, project, project, jsonProject);

		json const* filters = expectSub<json_vt::object>(jsonProject, "filters");
		if (filters)
		{
			for(auto filterIt : filters->items())
			{
				auto const& val = filterIt.value();
				if (val.type() == json_vt::object)
				{
					// Create and reference the configuration:
					Configuration& cfg = project.premakeFilters[filterIt.key()];
					loadConfigurationFromJSON(package_, project, cfg, val);
				}
			}
		}

		package_.projects.push_back(std::move(project));
	}

	return true;
}

/////////////////////////////////////////////////
std::size_t getNumElements(VecOfStr const& v)
{
	return v.size();
}

/////////////////////////////////////////////////
std::size_t getNumElements(VecOfStrAcc const& v)
{
	return v.public_.size() + v.private_.size() + v.interface_.size();
}


/////////////////////////////////////////////////
void computeConfiguration(Configuration& into_, Package const& fromPkg_, Project const& fromProject_, Configuration const& from_, AccessType mode_)
{
	auto resolvePath = [&](auto const& pathLikeElem)
		{
			return fromPkg_.resolvePath(fs::path(pathLikeElem)).string();
		};

	mergeAccesses(into_.defines, 			from_.defines, 		 		mode_);
	mergeAccesses(into_.includeFolders, 	from_.includeFolders,  		mode_, resolvePath);
	mergeAccesses(into_.linkerFolders, 		from_.linkerFolders,  		mode_, resolvePath);
	mergeAccesses(into_.linkedLibraries, 	from_.linkedLibraries, 		mode_);
	mergeAccesses(into_.compilerOptions, 	from_.compilerOptions, 		mode_);
	mergeAccesses(into_.linkerOptions, 		from_.linkerOptions, 		mode_);

	// TODO: case, enums
	if (fromProject_.type == Project::Type::StaticLib || fromProject_.type == Project::Type::SharedLib)
	{
		// Add dependency output folder:
		{
			auto& target = targetByAccessType(into_.linkerFolders.computed, mode_);
			target.push_back(fsx::fwd(fromPkg_.predictOutputFolder(fromProject_)).string());
		}

		// Add dependency file to linker:
		{
			auto& target = targetByAccessType(into_.linkedLibraries.computed, mode_);
			target.push_back(fromProject_.name);
		}
	}
}

///////////////////////////////////////////////////
// Private functions
///////////////////////////////////////////////////

///////////////////////////////////////////////////
void readDependencyAccess(Package &pkg_, Project & proj_, json const& deps_, std::vector<Dependency> &target_)
{
	using json_vt = json::value_t;

	if (deps_.type() != json_vt::array)
		throw PaccException("invalid type of dependencies subfield - array required");

	target_.reserve(deps_.size());

	for(auto item : deps_.items())
	{
		if (json const* rawDep = expect<json_vt::string>(item.value()))
		{
			std::string depPattern = rawDep->get<std::string>();
			if (startsWith(depPattern, "file:"))
			{
				target_.push_back( Dependency::raw( depPattern.substr(5) ) );
			}
			else if (startsWith(depPattern, "self:"))
			{
				SelfDependency sd { &proj_, depPattern.substr(5), &pkg_ };
				target_.push_back( Dependency::self( std::move(sd) ) );
			}
			else
			{
				DownloadLocation loc = DownloadLocation::parse(depPattern);

				PackageDependency pd;
				pd.packageName 		= loc.repository;

				try {
					pd.version = VersionReq::fromString(loc.branch);
				}
				catch(...) {} // just ignore

				pd.projects.push_back(loc.repository);
				pd.downloadLocation = std::move(depPattern);

				target_.push_back(
						Dependency::package( std::move(pd) )
					);
			}
		}
		else if (json const* pkgDep = expect<json_vt::object>(item.value()))
		{
			// Required fields:
			json const& name 		= requireSub<json_vt::string>(*pkgDep, "name");
			json const* projects 	= expectSub<json_vt::array>(*pkgDep, "projects");
			// Optional fields:
			json const* version 	= expectSub<json_vt::string>(*pkgDep, "version");

			// Configure dependency:
			PackageDependency pd;

			// Required:
			pd.packageName = name;

			// Parse download location:
			pd.downloadLocation = JsonView{*pkgDep}.stringFieldOr("from", "");
			auto loc = DownloadLocation::parse(pd.downloadLocation);

			if (projects)
			{
				pd.projects.reserve(projects->size());
				for(auto proj : projects->items())
				{
					json const& projName = require<json_vt::string>(proj.value());

					pd.projects.push_back(projName.get<std::string>());
				}
			}
			else
			{
				std::string originalName;

				if (!loc.repository.empty())
					originalName = loc.repository;
				else
					originalName = name;

				pd.projects.emplace_back(std::move(originalName));
			}

			// Optional
			if (version)
			{
				try {
					pd.version = VersionReq::fromString(version->get<std::string>());
				}
				catch (...) {
					pd.version.type = VersionReq::Any;
				}
			}

			target_.push_back(
					Dependency::package( std::move(pd) )
				);
		}
		else
			throw PaccException("Invalid dependency type");
	}


}


///////////////////////////////////////////////////
json const* selfOrSubfieldOpt(json const &self, std::string_view fieldName)
{
	if (fieldName == "")
		return &self;
	else
	{
		if (auto it = self.find(fieldName); it != self.end())
			return &it.value();
	}

	return nullptr;
}

///////////////////////////////////////////////////
json const& selfOrSubfieldReq(json const &self, std::string_view fieldName)
{
	json const* v = selfOrSubfieldOpt(self, fieldName);
	if (!v)
		throw PaccException("field {0} not found", fieldName);
	else
		return *v;
}

///////////////////////////////////////////////////
json const* selfOrSubfield(json const &self, std::string_view fieldName, bool required)
{
	if (required)
		return &selfOrSubfieldReq(self, fieldName);
	else
		return selfOrSubfieldOpt(self, fieldName);
}

///////////////////////////////////////////////////
VecOfStr loadVecOfStrField(json const &j, std::string_view fieldName, bool direct, bool required)
{
	using JV = JsonView;

	VecOfStr result;
	std::string const elemName = std::string(fieldName) + " element";

	// Either subfield or the `j` itself (direct => `j` is an array)
	json const* val = selfOrSubfield(j, direct ? "" : fieldName, required);

	// Can be null if `required` == false
	if (!val)
		return result;

	if (val->type() == json::value_t::string)
	{
		result.push_back(*val);
	}
	else
	{
		JV(*val).requireType(fieldName, json::value_t::array);

		// Read the array:
		result.reserve(val->size());

		for(auto elem : val->items())
		{
			JV{elem.value()}.requireType(elemName, json::value_t::string);
			result.push_back(elem.value());
		}
	}
	return result;
}

///////////////////////////////////////////////////
VecOfStrAcc loadVecOfStrAccField(json const &j, std::string_view fieldName, AccessType defaultAccess_)
{
	VecOfStrAcc result;
	if (auto it = j.find(fieldName); it != j.end())
	{
		json::value_t type = it.value().type();
		if (type == json::value_t::array || type == json::value_t::string)
			targetByAccessType(result, defaultAccess_) = loadVecOfStrField(*it, fieldName, true);
		else
		{
			result.private_ 	= loadVecOfStrField(*it, "private");
			result.public_ 		= loadVecOfStrField(*it, "public");
			result.interface_ 	= loadVecOfStrField(*it, "interface");
		}
	}
	return result;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const* expect(json const &j)
{
	if (j.type() == type)
		return &j;
	else
		return nullptr;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const* expectSub(json const &j, std::string_view subfieldName)
{
	auto it = j.find(subfieldName);
	if (it != j.end() && it->type() == type)
	{
		return (&(*it));
	}

	return nullptr;
}

///////////////////////////////////////////////////
template <json::value_t type>
json const& require(json const &j)
{
	if (j.type() == type)
		return j;
	else
		throw PaccException("invalid type");
}

///////////////////////////////////////////////////
template <json::value_t type>
json const& requireSub(json const &j, std::string_view subfieldName)
{
	auto it = j.find(subfieldName);
	if (it != j.end() && it->type() == type)
	{
		return (*it);
	}

	throw PaccException("invalid subfield type");
}
