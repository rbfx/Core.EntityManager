#pragma once

#include "EntityManager.h"

#include <Urho3D/Scene/Component.h>

#include <entt/entt.hpp>

namespace Urho3D
{

class EntityManager;

/// Connects Node to a specific entity in the EntityManager.
/// Don't create any other components on the same Node manually, they may get removed.
/// Nodes with EntityReference are managed by the EntityManager.
class PLUGIN_CORE_ENTITYMANAGER_API EntityReference : public TrackedComponent<TrackedComponentBase, EntityManager>
{
    URHO3D_OBJECT(EntityReference, Component);

public:
    EntityReference(Context* context);
    ~EntityReference() override;
    static void RegisterObject(Context* context);

    /// Implement Component.
    /// @{
    void ApplyAttributes() override;
    /// @}

    /// Render custom inspector UI.
    bool RenderInspector();

    void SetEntityInternal(entt::entity entity) { entity_ = entity; }
    entt::entity Entity() const { return entity_; }

    /// Attributes.
    /// @{
    unsigned GetEntityAttr() const { return static_cast<unsigned>(entity_); }
    void SetEntityAttr(unsigned entity) { entity_ = static_cast<entt::entity>(entity); }
    bool GetPlaceholderAttr() const { return false; }
    void SetPlaceholderAttr(bool placeholder);
    void SetDataAttr(const ByteVector& data);
    ByteVector GetDataAttr() const;
    /// @}

private:
    /// Implement Component.
    /// @{
    void OnMarkedDirty(Node* node) override;
    /// @}

    entt::entity entity_{entt::null};
};

} // namespace Urho3D
