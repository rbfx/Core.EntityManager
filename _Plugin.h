#pragma once

#include <Urho3D/Urho3D.h>

#if Plugin_Core_EntityManager_EXPORT
    #define PLUGIN_CORE_ENTITYMANAGER_API URHO3D_EXPORT_API
#else
    #define PLUGIN_CORE_ENTITYMANAGER_API URHO3D_IMPORT_API
#endif
