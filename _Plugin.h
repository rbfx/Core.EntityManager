#pragma once

#include <Urho3D/Urho3D.h>

#include <Urho3D/Container/ConstString.h>

#if Plugin_Core_EntityManager_EXPORT
    #define PLUGIN_CORE_ENTITYMANAGER_API URHO3D_EXPORT_API
#else
    #define PLUGIN_CORE_ENTITYMANAGER_API URHO3D_IMPORT_API
#endif

namespace Urho3D
{

URHO3D_GLOBAL_CONSTANT(ConstString Category_Plugin_EntityManager{"Component/Plugin/Core.EntityManager"});

}
