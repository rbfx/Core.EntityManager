#pragma once

#include "_Plugin.h"

#include <Urho3D/Core/NonCopyable.h>
#include <Urho3D/Core/Signal.h>
#include <Urho3D/Core/TypeTrait.h>
#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Scene/TrackedComponent.h>

#include <entt/entt.hpp>

#include <EASTL/unique_ptr.h>

// Support formatting for entt::entity.
template <> struct fmt::formatter<entt::entity>
{
    constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
    {
        // TODO: Support formatting specifiers
        return ctx.begin();
    }

    auto format(const entt::entity& value, format_context& ctx) const -> format_context::iterator
    {
        return fmt::format_to(
            ctx.out(), "{}:{}", static_cast<unsigned>(entt::to_entity(value)), entt::to_version(value));
    }
};

namespace Urho3D
{

class EntityReference;

/// Component that is used to tag currently materialized entities.
/// EntityReference is expected to be valid.
struct EntityMaterialized
{
    WeakPtr<EntityReference> entityReference_;
};

/// Component that is used to tag entities with updated transforms.
/// It is up to the user to clear this component when it's not needed anymore.
struct EntityTransformDirty
{
    static constexpr unsigned Version = 1;
    void SerializeInBlock(Archive& archive, unsigned version) {}
    bool RenderInspector() { return false; }
};

/// Interface to manage EnTT components.
class PLUGIN_CORE_ENTITYMANAGER_API EntityComponentFactory
{
public:
    EntityComponentFactory(const ea::string& name);
    virtual ~EntityComponentFactory() = default;

    const ea::string& GetName() const { return name_; }

    virtual bool IsEmpty() const = 0;
    virtual unsigned GetVersion() const = 0;

    virtual bool HasComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void CreateComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void DestroyComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void CopyComponents(const entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities) = 0;
    virtual void MoveComponents(entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities) = 0;

    virtual void SerializeComponent(
        Archive& archive, entt::registry& registry, entt::entity entity, unsigned version) = 0;
    virtual void SerializeComponents(Archive& archive, entt::registry& registry, unsigned version) = 0;
    virtual void SaveComponentsPartial(
        Archive& archive, entt::registry& registry, unsigned version, const ea::vector<entt::entity>& entities) = 0;

    virtual bool RenderUI(entt::registry& registry, entt::entity entity) = 0;
    virtual void CommitActions(entt::registry& registry) = 0;

private:
    ea::string name_;
};

/// Type-erased manager of component types for registry.
class PLUGIN_CORE_ENTITYMANAGER_API ComponentTypeManager : public MovableNonCopyable
{
public:
    using EntityComponentFactoryVector = ea::vector<ea::unique_ptr<EntityComponentFactory>>;

    /// Register new EnTT component type.
    /// It should be done as soon as possible, preferably in the constructor of derived class.
    void AddComponentType(ea::unique_ptr<EntityComponentFactory> factory);
    template <class T> void AddComponentType(const ea::string& name);
    template <class T> void AddComponentType();
    EntityComponentFactory* FindComponentType(ea::string_view name) const;

    const EntityComponentFactoryVector& GetComponentTypes() const { return componentFactories_; }
    const EntityComponentFactoryVector& GetComponentTypesSorted();

    /// Ensure order of factories for consistent output.
    void EnsureComponentTypesSorted();
    /// Serialize entire registry contents. All existing entities are overwritten.
    void SerializeRegistry(Archive& archive, entt::registry& registry) const;
    /// Serialize partial registry contents. Only save is supported. Load is compatible with SerializeRegistry.
    void SaveRegistryPartial(
        Archive& archive, entt::registry& registry, const ea::vector<entt::entity>& entities) const;
    /// Serialize components of specific entity. Entity must exist. All existing components are overwritten.
    void SerializeStandaloneEntity(Archive& archive, entt::registry& registry, entt::entity entity) const;
    /// Move entities from registry to registry. Materialization status is not copied.
    void MoveEntities(entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, ea::vector<entt::entity>& toEntities) const;
    /// Copy entities from registry to registry. Materialization status is not copied.
    void CopyEntities(const entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, ea::vector<entt::entity>& toEntities) const;

    /// Utilities.
    /// @{
    static unsigned GetEntityVersion(entt::entity entity);
    static unsigned GetEntityIndex(entt::entity entity);
    template <class T>
    static void SerializeComponents(Archive& archive, const char* name, entt::registry& registry, unsigned version);
    template <class T>
    static void SaveComponentsPartial(Archive& archive, const char* name, entt::registry& registry, unsigned version,
        const ea::vector<entt::entity>& entities);
    /// @}

private:
    void SerializeEntities(Archive& archive, entt::registry& registry) const;
    void SerializeUserComponents(Archive& archive, entt::registry& registry) const;

    void SaveEntitiesPartial(
        Archive& archive, entt::registry& registry, const ea::vector<entt::entity>& entities) const;
    void SaveUserComponentsPartial(
        Archive& archive, entt::registry& registry, const ea::vector<entt::entity>& entities) const;

    /// Comparator to sort entities by their index.
    struct EntityIndexComparator
    {
        bool operator()(entt::entity lhs, entt::entity rhs) const
        {
            return ComponentTypeManager::GetEntityIndex(lhs) < ComponentTypeManager::GetEntityIndex(rhs);
        }
    };

private:
    EntityComponentFactoryVector componentFactories_;
    bool componentTypesSorted_{};
};

/// Subsystem that stores and manages EnTT entities.
/// Don't remove this component from the scene if you have any entities!
class PLUGIN_CORE_ENTITYMANAGER_API EntityManager
    : public TrackedComponentRegistryBase
    , public ComponentTypeManager
{
    URHO3D_OBJECT(EntityManager, TrackedComponentRegistryBase);

public:
    Signal<void(entt::registry& registry, entt::entity entity, EntityReference* reference)> OnEntityMaterialized;
    Signal<void(entt::registry& registry, entt::entity entity, EntityReference* reference)> OnEntityDematerialized;
    Signal<void(entt::registry& registry)> OnPostUpdateSynchronized;

    EntityManager(Context* context);
    static void RegisterObject(Context* context);

    void ApplyAttributes() override;
    bool HasAuxiliaryData() const override { return true; }
    void SerializeAuxiliaryData(Archive& archive) override;

    /// Support inspector UI.
    /// @{
    bool RenderManagerInspector();
    bool RenderEntityInspector(entt::entity entity);
    void CommitActions();
    /// @}

    /// Synchronize pending EntityReference additions with the registry.
    void Synchronize();

    bool IsEntityValid(entt::entity entity) const;
    EntityReference* EntityToReference(entt::entity entity) const;
    Node* EntityToNode(entt::entity entity) const;
    entt::entity NodeToEntity(Node* node) const;

    static Variant EntityToVariant(entt::entity entity);
    static entt::entity VariantToEntity(const Variant& variant);

    bool IsEntityMaterialized(entt::entity entity) const;
    EntityReference* MaterializeEntity(entt::entity entity);
    void DematerializeEntity(entt::entity entity);

    /// Per-entity serialization. Use with caution.
    /// @{
    ByteVector EncodeEntity(entt::registry& registry, entt::entity entity);
    void DecodeEntity(entt::registry& registry, entt::entity entity, const ByteVector& data);

    ea::vector<entt::entity> GetEntities() const;
    ByteVector EncodeEntity(entt::entity entity);
    void DecodeEntity(entt::entity entity, const ByteVector& data);
    void QueueDecodeEntity(EntityReference* entityReference, const ByteVector& data);
    /// @}

    /// Getters.
    /// @{
    entt::registry& Registry() { return registry_; }
    /// @}

    /// Attributes.
    /// @{
    void SetDataAttr(const ByteVector& data);
    ByteVector GetDataAttr() const;
    bool GetPlaceholderAttr() const { return false; }
    void SetPlaceholderAttr(bool placeholder);

    const ea::string& GetEntitiesContainerName() const { return entitiesContainerName_; }
    void SetEntitiesContainerName(const ea::string& value) { entitiesContainerName_ = value; }

    bool GetUseTemporaryNodes() const { return useTemporaryNodes_; }
    void SetUseTemporaryNodes(bool value);

    bool GetRestoreNodesOnLoad() const { return restoreNodesOnLoad_; }
    void SetRestoreNodesOnLoad(bool value) { restoreNodesOnLoad_ = value; }
    /// @}

protected:
    /// Implement TrackedComponentRegistryBase.
    /// @{
    void OnComponentAdded(TrackedComponentBase* baseComponent) override;
    void OnComponentRemoved(TrackedComponentBase* baseComponent) override;
    void OnAddedToScene(Scene* scene) override;
    void OnRemovedFromScene() override;
    /// @}

    /// Return display label for the entity.
    virtual ea::string GetEntityLabel(entt::entity entity) const;
    /// Post-update synchronization. Executed even if the Scene is paused.
    virtual void ForcedPostUpdate();
    /// Begin update from Inspector.
    virtual void BeginInspectorUpdate() {}
    /// End update from Inspector.
    virtual void EndInspectorUpdate() {}

    entt::registry registry_;

private:
    void EnsureEntitiesMaterialized();
    void MarkTemporaryStatusDirty() { temporaryStatusDirty_ = true; }

    void RenderEntityHeader(entt::entity entity);
    EntityComponentFactory* RenderCreateComponent(entt::entity entity);
    bool RenderExistingComponents(entt::entity entity);

    ea::string entitiesContainerName_;
    bool useTemporaryNodes_{};
    bool restoreNodesOnLoad_{true};
    WeakPtr<Node> entitiesContainer_;

    bool registryDirty_{};
    bool temporaryStatusDirty_{};
    ea::unordered_set<WeakPtr<EntityReference>> pendingEntitiesAdded_;
    ea::vector<ea::pair<WeakPtr<EntityReference>, ByteVector>> pendingEntityDecodes_;
    bool synchronizationInProgress_{};
    bool suppressComponentEvents_{};

    struct EditorUI
    {
        ea::vector<ea::pair<entt::entity, bool>> pendingMaterializations_;
        ea::vector<ea::pair<entt::entity, EntityComponentFactory*>> pendingCreateComponents_;
        ea::vector<ea::pair<entt::entity, EntityComponentFactory*>> pendingDestroyComponents_;
        ea::vector<EntityComponentFactory*> pendingEditComponents_;
    } ui_;
};

/// Check whether component has "RenderInspector" member function.
URHO3D_TYPE_TRAIT(HasRenderInspector, !!std::declval<T&>().RenderInspector());

/// Default implementation of EntityComponentFactory.
/// T is expected to have certain functions and static members.
template <class T> class DefaultEntityComponentFactory : public EntityComponentFactory
{
public:
    using EntityComponentFactory::EntityComponentFactory;

    /// Implement EntityComponentFactory.
    /// @{
    bool IsEmpty() const override { return std::is_empty_v<T>; }
    unsigned GetVersion() const override { return T::Version; }

    bool HasComponent(entt::registry& registry, entt::entity entity) override;
    void CreateComponent(entt::registry& registry, entt::entity entity) override;
    void DestroyComponent(entt::registry& registry, entt::entity entity) override;
    void CopyComponents(const entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities) override;
    void MoveComponents(entt::registry& fromRegistry, entt::registry& toRegistry,
        const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities) override;

    void SerializeComponent(Archive& archive, entt::registry& registry, entt::entity entity, unsigned version) override;
    void SerializeComponents(Archive& archive, entt::registry& registry, unsigned version) override;
    void SaveComponentsPartial(Archive& archive, entt::registry& registry, unsigned version,
        const ea::vector<entt::entity>& entities) override;

    bool RenderUI(entt::registry& registry, entt::entity entity) override;
    void CommitActions(entt::registry& registry) override;
    /// @}

private:
    template <bool IsMove>
    void CopyOrMoveComponents(ea::conditional_t<IsMove, entt::registry, const entt::registry>& fromRegistry,
        entt::registry& toRegistry, const ea::vector<entt::entity>& fromEntities,
        const ea::vector<entt::entity>& toEntities);

private:
    ea::string name_;

    struct PendingEditAction
    {
        entt::entity entity_;
        T newValue_;
    };
    ea::vector<PendingEditAction> pendingEditActions_;
};

} // namespace Urho3D

namespace Urho3D
{

template <class T> void ComponentTypeManager::AddComponentType(const ea::string& name)
{
    AddComponentType(ea::make_unique<DefaultEntityComponentFactory<T>>(name));
}

template <class T> void ComponentTypeManager::AddComponentType()
{
    AddComponentType<T>(T::TypeName);
}

template <class T>
void ComponentTypeManager::SerializeComponents(
    Archive& archive, const char* name, entt::registry& registry, unsigned version)
{
    auto& storage = registry.storage<T>();
    const auto numComponents = static_cast<unsigned>(storage.size());

    const auto block = archive.OpenArrayBlock(name, numComponents);
    if (archive.IsInput())
    {
        for (unsigned i = 0; i < block.GetSizeHint(); ++i)
        {
            const auto elementBlock = archive.OpenUnorderedBlock("component");

            unsigned entityData = 0;
            archive.Serialize("_entity", entityData);
            const auto entity = static_cast<entt::entity>(entityData);

            if constexpr (!std::is_empty_v<T>)
            {
                auto& component = registry.emplace_or_replace<T>(entity);
                component.SerializeInBlock(archive, version);
            }
            else
            {
                registry.emplace_or_replace<T>(entity);
            }
        }
    }
    else
    {
        static thread_local ea::vector<entt::entity> entitiesBuffer;
        auto& entities = entitiesBuffer;

        const auto view = registry.view<T>();
        entities.assign(view.begin(), view.end());
        ea::sort(entities.begin(), entities.end(), EntityIndexComparator{});

        for (const entt::entity entity : entities)
        {
            const auto elementBlock = archive.OpenUnorderedBlock("component");

            auto entityData = static_cast<unsigned>(entity);
            archive.Serialize("_entity", entityData);

            if constexpr (!std::is_empty_v<T>)
            {
                auto& component = registry.get<T>(entity);
                component.SerializeInBlock(archive, version);
            }
        };
    }
}

template <class T>
void ComponentTypeManager::SaveComponentsPartial(Archive& archive, const char* name, entt::registry& registry,
    unsigned version, const ea::vector<entt::entity>& entities)
{
    URHO3D_ASSERT(!archive.IsInput());

    // Collect entities with component T
    static thread_local ea::vector<entt::entity> filteredEntitiesBuffer;
    auto& filteredEntities = filteredEntitiesBuffer;

    filteredEntities.clear();
    for (const entt::entity entity : entities)
    {
        if (registry.valid(entity) && registry.any_of<T>(entity))
            filteredEntities.push_back(entity);
    }

    // Serialize components normally
    const auto block = archive.OpenArrayBlock(name, filteredEntities.size());
    for (const entt::entity entity : filteredEntities)
    {
        const auto elementBlock = archive.OpenUnorderedBlock("component");

        auto entityData = static_cast<unsigned>(entity);
        archive.Serialize("_entity", entityData);

        if constexpr (!std::is_empty_v<T>)
        {
            auto& component = registry.get<T>(entity);
            component.SerializeInBlock(archive, version);
        }
    };
}

template <class T> bool DefaultEntityComponentFactory<T>::HasComponent(entt::registry& registry, entt::entity entity)
{
    const auto& storage = registry.storage<T>();
    return storage.contains(entity);
}

template <class T> void DefaultEntityComponentFactory<T>::CreateComponent(entt::registry& registry, entt::entity entity)
{
    (void)registry.emplace<T>(entity);
}

template <class T>
void DefaultEntityComponentFactory<T>::DestroyComponent(entt::registry& registry, entt::entity entity)
{
    registry.remove<T>(entity);
}

template <class T>
void DefaultEntityComponentFactory<T>::SerializeComponent(
    Archive& archive, entt::registry& registry, entt::entity entity, unsigned version)
{
    if constexpr (!std::is_empty_v<T>)
    {
        auto& component = registry.get<T>(entity);
        component.SerializeInBlock(archive, version);
    }
}

template <class T>
void DefaultEntityComponentFactory<T>::SerializeComponents(Archive& archive, entt::registry& registry, unsigned version)
{
    EntityManager::SerializeComponents<T>(archive, "components", registry, version);
}

template <class T>
void DefaultEntityComponentFactory<T>::SaveComponentsPartial(
    Archive& archive, entt::registry& registry, unsigned version, const ea::vector<entt::entity>& entities)
{
    EntityManager::SaveComponentsPartial<T>(archive, "components", registry, version, entities);
}

template <class T> bool DefaultEntityComponentFactory<T>::RenderUI(entt::registry& registry, entt::entity entity)
{
    if constexpr (!std::is_empty_v<T> && HasRenderInspector<T>::value)
    {
        T& component = registry.get<T>(entity);
        const T backup = component;
        if (component.RenderInspector())
        {
            PendingEditAction action;
            action.entity_ = entity;
            action.newValue_ = component;
            component = backup;
            pendingEditActions_.push_back(action);
            return true;
        }
    }
    return false;
}

template <class T> void DefaultEntityComponentFactory<T>::CommitActions(entt::registry& registry)
{
    for (const PendingEditAction& action : pendingEditActions_)
    {
        if (!registry.valid(action.entity_))
        {
            URHO3D_LOGERROR("Cannot edit component '{}' in entity {}", GetName(), action.entity_);
            continue;
        }

        registry.replace<T>(action.entity_, action.newValue_);
    }
    pendingEditActions_.clear();
}

template <class T>
void DefaultEntityComponentFactory<T>::CopyComponents(const entt::registry& fromRegistry, entt::registry& toRegistry,
    const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities)
{
    CopyOrMoveComponents<false>(fromRegistry, toRegistry, fromEntities, toEntities);
}

template <class T>
void DefaultEntityComponentFactory<T>::MoveComponents(entt::registry& fromRegistry, entt::registry& toRegistry,
    const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities)
{
    CopyOrMoveComponents<true>(fromRegistry, toRegistry, fromEntities, toEntities);
}

template <class T>
template <bool IsMove>
void DefaultEntityComponentFactory<T>::CopyOrMoveComponents(
    ea::conditional_t<IsMove, entt::registry, const entt::registry>& fromRegistry, entt::registry& toRegistry,
    const ea::vector<entt::entity>& fromEntities, const ea::vector<entt::entity>& toEntities)
{
    URHO3D_ASSERT(fromEntities.size() == toEntities.size());
    for (unsigned i = 0; i < fromEntities.size(); ++i)
    {
        const entt::entity fromEntity = fromEntities[i];
        const entt::entity toEntity = toEntities[i];

        if (!fromRegistry.template any_of<T>(fromEntity))
            continue;

        if constexpr (!std::is_empty_v<T>)
        {
            if constexpr (IsMove)
                toRegistry.emplace_or_replace<T>(toEntity, ea::move(fromRegistry.template get<T>(fromEntity)));
            else
                toRegistry.emplace_or_replace<T>(toEntity, fromRegistry.template get<T>(fromEntity));
        }
        else
        {
            toRegistry.emplace_or_replace<T>(toEntity);
        }
    }
}

} // namespace Urho3D
