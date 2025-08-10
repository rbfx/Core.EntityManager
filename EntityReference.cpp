#include "EntityReference.h"

#include "EntityManager.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/SystemUI/Widgets.h>

namespace Urho3D
{

namespace
{

const auto nullEntity = static_cast<unsigned>(entt::entity{entt::null});

}

EntityReference::EntityReference(Context* context)
    : TrackedComponent<TrackedComponentBase, EntityManager>(context)
{
}

EntityReference::~EntityReference()
{
}

void EntityReference::RegisterObject(Context* context)
{
    context->RegisterFactory<EntityReference>(Category_Plugin_EntityManager);

    URHO3D_ACCESSOR_ATTRIBUTE("Entity", GetEntityAttr, SetEntityAttr, unsigned, nullEntity, AM_DEFAULT | AM_NOEDIT);

    // Artificial attribute used to support per-entity manipulation.
    URHO3D_ACCESSOR_ATTRIBUTE("Data", GetDataAttr, SetDataAttr, ByteVector, Variant::emptyBuffer, AM_TEMPORARY | AM_NOEDIT);

    // Artificial attribute that is used to attach custom inspector UI.
    URHO3D_ACCESSOR_ATTRIBUTE("Placeholder", GetPlaceholderAttr, SetPlaceholderAttr, bool, false, AM_EDIT)
        .SetScopeHint(AttributeScopeHint::Serializable);
}

void EntityReference::ApplyAttributes()
{
    EntityManager* manager = GetRegistry();
    if (manager)
        manager->Synchronize();
}

bool EntityReference::RenderInspector()
{
    EntityManager* manager = GetRegistry();
    if (manager && entity_ != entt::null)
        return manager->RenderEntityInspector(entity_);
    return false;
}

void EntityReference::SetPlaceholderAttr(bool placeholder)
{
    EntityManager* manager = GetRegistry();
    if (manager)
        manager->CommitActions();
}

void EntityReference::SetDataAttr(const ByteVector& data)
{
    EntityManager* manager = GetRegistry();
    if (manager && entity_ != entt::null)
        manager->QueueDecodeEntity(this, data);
}

ByteVector EntityReference::GetDataAttr() const
{
    EntityManager* manager = GetRegistry();
    if (manager && entity_ != entt::null)
        return manager->EncodeEntity(entity_);
    return {};
}

void EntityReference::OnMarkedDirty(Node* node)
{
    EntityManager* manager = GetRegistry();
    if (manager && entity_ != entt::null)
    {
        auto& registry = manager->Registry();
        if (!registry.valid(entity_))
            return;

        registry.emplace_or_replace<EntityTransformDirty>(entity_);
    }
}

} // namespace Urho3D
