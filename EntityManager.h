#pragma once

#include "_Plugin.h"

#include <Urho3D/Core/Signal.h>
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
};

/// Interface to manage EnTT components.
class PLUGIN_CORE_ENTITYMANAGER_API EntityComponentFactory
{
public:
    EntityComponentFactory(const ea::string& name);
    const ea::string& GetName() const { return name_; }

    virtual bool IsEmpty() const = 0;
    virtual unsigned GetVersion() const = 0;
    virtual bool HasComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void CreateComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void DestroyComponent(entt::registry& registry, entt::entity entity) = 0;
    virtual void SerializeComponent(
        Archive& archive, entt::registry& registry, entt::entity entity, unsigned version) = 0;
    virtual void SerializeComponents(Archive& archive, entt::registry& registry, unsigned version) = 0;
    virtual bool RenderUI(entt::registry& registry, entt::entity entity) = 0;
    virtual void CommitActions(entt::registry& registry) = 0;

private:
    ea::string name_;
};

/// Subsystem that stores and manages EnTT entities.
/// Don't remove this component from the scene if you have any entities!
class PLUGIN_CORE_ENTITYMANAGER_API EntityManager : public TrackedComponentRegistryBase
{
    URHO3D_OBJECT(EntityManager, TrackedComponentRegistryBase);

public:
    Signal<void(entt::registry& registry, entt::entity entity, EntityReference* reference)> OnEntityMaterialized;
    Signal<void(entt::registry& registry, entt::entity entity, EntityReference* reference)> OnEntityDematerialized;
    Signal<void(entt::registry& registry)> OnManagerUpdated;

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

    /// Register new EnTT component type.
    /// It should be done as soon as possible, preferably in the constructor of derived class.
    void AddComponentType(ea::unique_ptr<EntityComponentFactory> factory);
    template <class T> void AddComponentType(const ea::string& name);
    EntityComponentFactory* FindComponentType(ea::string_view name) const;

    bool IsEntityMaterialized(entt::entity entity) const;
    EntityReference* GetEntityReference(entt::entity entity) const;
    void MaterializeEntity(entt::entity entity);
    void DematerializeEntity(entt::entity entity);

    /// Per-entity serialization. Use with caution.
    /// @{
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
    /// @}

    /// Utilities.
    /// @{
    static unsigned GetEntityVersion(entt::entity entity);
    static unsigned GetEntityIndex(entt::entity entity);
    template <class T>
    static void SerializeComponents(Archive& archive, const char* name, entt::registry& registry, unsigned version);
    /// @}

protected:
    virtual ea::string GetEntityLabel(entt::entity entity) const;

    entt::registry registry_;

private:
    /// Comparator to sort entities by their index.
    struct EntityIndexComparator
    {
        bool operator()(entt::entity lhs, entt::entity rhs) const
        {
            return EntityManager::GetEntityIndex(lhs) < EntityManager::GetEntityIndex(rhs);
        }
    };

    /// Implement TrackedComponentRegistryBase.
    /// @{
    void OnComponentAdded(TrackedComponentBase* baseComponent) override;
    void OnComponentRemoved(TrackedComponentBase* baseComponent) override;
    /// @}

    void EnsureComponentTypesSorted();
    void EnsureEntitiesMaterialized();

    void SerializeRegistry(Archive& archive);
    void SerializeEntities(Archive& archive);
    void SerializeUserComponents(Archive& archive);
    void SerializeStandaloneEntity(Archive& archive, entt::entity entity);

    void RenderEntityHeader(entt::entity entity);
    EntityComponentFactory* RenderCreateComponent(entt::entity entity);
    bool RenderExistingComponents(entt::entity entity);

    void Update();

    ea::string entitiesContainerName_;
    WeakPtr<Node> entitiesContainer_;

    ea::vector<ea::unique_ptr<EntityComponentFactory>> componentFactories_;
    bool componentTypesSorted_{};

    bool registryDirty_{};
    ea::unordered_set<WeakPtr<EntityReference>> pendingEntitiesAdded_;
    ea::vector<ea::pair<WeakPtr<EntityReference>, ByteVector>> pendingEntityDecodes_;
    bool synchronizationInProgress_{};

    struct EditorUI
    {
        ea::vector<ea::pair<entt::entity, bool>> pendingMaterializations_;
        ea::vector<ea::pair<entt::entity, EntityComponentFactory*>> pendingCreateComponents_;
        ea::vector<ea::pair<entt::entity, EntityComponentFactory*>> pendingDestroyComponents_;
        ea::vector<EntityComponentFactory*> pendingEditComponents_;
    } ui_;
};

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
    void SerializeComponent(Archive& archive, entt::registry& registry, entt::entity entity, unsigned version) override;
    void SerializeComponents(Archive& archive, entt::registry& registry, unsigned version) override;
    bool RenderUI(entt::registry& registry, entt::entity entity) override;
    void CommitActions(entt::registry& registry) override;
    /// @}

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

template <class T> void EntityManager::AddComponentType(const ea::string& name)
{
    AddComponentType(ea::make_unique<DefaultEntityComponentFactory<T>>(name));
}

template <class T>
void EntityManager::SerializeComponents(Archive& archive, const char* name, entt::registry& registry, unsigned version)
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

template <class T> bool DefaultEntityComponentFactory<T>::RenderUI(entt::registry& registry, entt::entity entity)
{
    if constexpr (!std::is_empty_v<T>)
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

} // namespace Urho3D
