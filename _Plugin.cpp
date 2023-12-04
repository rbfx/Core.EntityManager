#include "_Plugin.h"

#include "EntityManager.h"
#include "EntityReference.h"

#include <Urho3D/Plugins/PluginApplication.h>
#include <Urho3D/SystemUI/SerializableInspectorWidget.h>

using namespace Urho3D;

namespace
{

void RegisterPluginObjects(PluginApplication& plugin)
{
    plugin.RegisterObject<EntityManager>();
    plugin.RegisterObject<EntityReference>();

    SerializableInspectorWidget::RegisterAttributeHook<EntityManager>("Placeholder",
        [](const AttributeHookContext& ctx, Variant& boxedValue)
    {
        if (ctx.objects_->size() == 1)
        {
            const auto entityManager = dynamic_cast<EntityManager*>(ctx.objects_->front().Get());
            if (entityManager->RenderManagerInspector())
            {
                boxedValue = true;
                return true;
            }
        }
        return false;
    });

    SerializableInspectorWidget::RegisterAttributeHook<EntityReference>("Placeholder",
        [](const AttributeHookContext& ctx, Variant& boxedValue)
    {
        if (ctx.objects_->size() == 1)
        {
            const auto entityReference = dynamic_cast<EntityReference*>(ctx.objects_->front().Get());
            if (entityReference->RenderInspector())
            {
                boxedValue = true;
                return true;
            }
        }
        return false;
    });
}

void UnregisterPluginObjects(PluginApplication& plugin)
{
    SerializableInspectorWidget::UnregisterAttributeHook<EntityManager>("Placeholder");
    SerializableInspectorWidget::UnregisterAttributeHook<EntityReference>("Placeholder");
}

} // namespace

URHO3D_DEFINE_PLUGIN_MAIN_SIMPLE(RegisterPluginObjects, UnregisterPluginObjects);
