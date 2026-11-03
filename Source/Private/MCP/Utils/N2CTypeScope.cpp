// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CTypeScope.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "PluginDescriptor.h"
#include "Misc/PackageName.h"

namespace N2CTypeScopePrivate
{
	EN2CTypeScope PluginScope(const IPlugin& Plugin)
	{
		return Plugin.GetLoadedFrom() == EPluginLoadedFrom::Engine ? EN2CTypeScope::EnginePlugin : EN2CTypeScope::ProjectPlugin;
	}
}

FN2CTypeScope::FN2CTypeScope()
{
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
	{
		const EN2CTypeScope Scope = N2CTypeScopePrivate::PluginScope(*Plugin);
		for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
		{
			ModuleScopes.Add(Module.Name, Scope);
		}
	}
	if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
	{
		for (const FModuleDescriptor& Module : Project->Modules)
		{
			ModuleScopes.Add(Module.Name, EN2CTypeScope::Project);
		}
	}
}

EN2CTypeScope FN2CTypeScope::Classify(const FString& Path) const
{
	// Export text names the object path between quotes
	FString ObjectPath = Path.Contains(TEXT("'")) ? FPackageName::ExportTextPathToObjectPath(Path) : Path;
	ObjectPath.TrimStartAndEndInline();
	ObjectPath.RemoveFromStart(TEXT("/"));
	int32 Slash = INDEX_NONE;
	ObjectPath.FindChar(TEXT('/'), Slash);
	const FString Root = Slash == INDEX_NONE ? ObjectPath : ObjectPath.Left(Slash);

	if (Root == TEXT("Script"))
	{
		// /Script/<Module>.<Type>[:<Sub>]
		FString Module = Slash == INDEX_NONE ? FString() : ObjectPath.Mid(Slash + 1);
		int32 End = INDEX_NONE;
		if (Module.FindChar(TEXT('.'), End) || Module.FindChar(TEXT(':'), End) || Module.FindChar(TEXT('/'), End))
		{
			Module.LeftInline(End);
		}
		const EN2CTypeScope* Owner = Module.IsEmpty() ? nullptr : ModuleScopes.Find(FName(*Module));
		return Owner ? *Owner : EN2CTypeScope::Engine;
	}
	if (Root == TEXT("Engine"))
	{
		return EN2CTypeScope::Engine;
	}
	if (Root == TEXT("Game") || Root.IsEmpty())
	{
		return EN2CTypeScope::Project;
	}
	// A plugin's content is mounted at /<PluginName>/
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Root);
	if (Plugin.IsValid() && Plugin->CanContainContent())
	{
		return N2CTypeScopePrivate::PluginScope(*Plugin);
	}
	return EN2CTypeScope::Project;
}

const TCHAR* FN2CTypeScope::ToString(EN2CTypeScope Scope)
{
	switch (Scope)
	{
	case EN2CTypeScope::Engine: return TEXT("engine");
	case EN2CTypeScope::EnginePlugin: return TEXT("engine_plugin");
	case EN2CTypeScope::ProjectPlugin: return TEXT("project_plugin");
	default: return TEXT("project");
	}
}
