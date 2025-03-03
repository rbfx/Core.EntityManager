#include "EntityManager.h"

#include "EntityReference.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/IO/Base64Archive.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/SystemUI/Widgets.h>

#include <IconFontCppHeaders/IconsFontAwesome6.h>

#include <SDL_clipboard.h>

namespace Urho3D
{

namespace
{

struct MaterializationStatus
{
    bool materialized_{};

    void SerializeInBlock(Archive& archive, unsigned version)
    {
        SerializeValue(archive, "materialized", materialized_);
        //
    }
};

const ea::string defaultContainerName = "Entities";

void FlattenEntityHierarchy(EntityReference* entityReference)
{
    Node* node = entityReference->GetNode();
    Node* parentNode = node->GetParent();

    // TODO: Ignore indirect children
    static thread_local ea::vector<EntityReference*> childrenReferences;
    node->FindComponents<EntityReference>(childrenReferences);
    for (EntityReference* childReference : childrenReferences)
        childReference->GetNode()->SetParent(parentNode);
}

} // namespace

EntityComponentFactory::EntityComponentFactory(const ea::string& name)
    : name_(name)
{
}

EntityManager::EntityManager(Context* context)
    : TrackedComponentRegistryBase(context, EntityReference::GetTypeStatic())
    , entitiesContainerName_(defaultContainerName)
{
}

void EntityManager::RegisterObject(Context* context)
{
    URHO3D_ATTRIBUTE("Entities Container Node", ea::string, entitiesContainerName_, defaultContainerName, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Data", GetDataAttr, SetDataAttr, ByteVector, Variant::emptyBuffer, AM_TEMPORARY | AM_NOEDIT);

    // Artificial attribute that is used to attach custom inspector UI.
    URHO3D_ACCESSOR_ATTRIBUTE("Placeholder", GetPlaceholderAttr, SetPlaceholderAttr, bool, false, AM_EDIT)
        .SetScopeHint(AttributeScopeHint::Serializable);
}

void EntityManager::ApplyAttributes()
{
    entitiesContainer_ = GetScene()->GetChild(entitiesContainerName_);
    if (!entitiesContainer_)
        entitiesContainer_ = GetScene()->CreateChild(entitiesContainerName_);

    Synchronize();
}

void EntityManager::SerializeAuxiliaryData(Archive& archive)
{
    SerializeRegistry(archive);
    if (archive.IsInput())
        registryDirty_ = true;
}

ea::string EntityManager::GetEntityLabel(entt::entity entity) const
{
    return Format("{}", entity);
}

bool EntityManager::RenderManagerInspector()
{
    bool changed = false;
    ui::Indent();

    {
        ColorScopeGuard colorScopeGuard{ImGuiCol_Text, Color::YELLOW};
        ui::Text("Materialized Entities:");
    }
    if (ui::BeginListBox("##Entities"))
    {
        for (const auto& [entity] : registry_.storage<entt::entity>().each())
        {
            const IdScopeGuard guard{entt::to_integral(entity)};
            const ea::string label = GetEntityLabel(entity);
            bool isMaterialized = IsEntityMaterialized(entity);
            if (ui::Checkbox(label.c_str(), &isMaterialized))
            {
                ui_.pendingMaterializations_.emplace_back(entity, isMaterialized);
                changed = true;
            }
        };
        ui::EndListBox();
    }

    if (ui::Button(ICON_FA_SQUARE_PLUS " Add Entity"))
    {
        const entt::entity entity = registry_.create();
        MaterializeEntity(entity);
    }

    ui::Unindent();
    return changed;
}

void EntityManager::SetPlaceholderAttr(bool placeholder)
{
    CommitActions();
}

void EntityManager::EnsureComponentTypesSorted()
{
    if (!componentTypesSorted_)
    {
        ea::sort(componentFactories_.begin(), componentFactories_.end(),
            [](const auto& lhs, const auto& rhs) { return lhs->GetName() < rhs->GetName(); });
        componentTypesSorted_ = true;
    }
}

bool EntityManager::RenderEntityInspector(entt::entity entity)
{
    EnsureComponentTypesSorted();

    bool changed = false;
    ui::Indent();

    RenderEntityHeader(entity);
    if (RenderExistingComponents(entity))
        changed = true;
    if (EntityComponentFactory* factory = RenderCreateComponent(entity))
    {
        ui_.pendingCreateComponents_.emplace_back(entity, factory);
        changed = true;
    }

    ui::Unindent();
    return changed;
}

void EntityManager::RenderEntityHeader(entt::entity entity)
{
    ColorScopeGuard colorScopeGuard{ImGuiCol_Text, Color::YELLOW};
    ui::Text("Entity %s", Format("{}", entity).c_str());

    ui::SameLine();
    if (ui::Button(ICON_FA_COPY "##CopyEntityID"))
        SDL_SetClipboardText(Format("{}", static_cast<unsigned>(entity)).c_str());
    if (ui::IsItemHovered())
        ui::SetTooltip("Copy entity ID to clipboard");
}

EntityComponentFactory* EntityManager::RenderCreateComponent(entt::entity entity)
{
    ui::BeginDisabled(componentFactories_.empty());
    if (ui::Button(ICON_FA_SQUARE_PLUS " Add EnTT Component"))
        ui::OpenPopup("##AddEnTTComponent");
    ui::EndDisabled();

    EntityComponentFactory* result = nullptr;
    if (ui::BeginPopup("##AddEnTTComponent"))
    {
        for (const auto& factory : componentFactories_)
        {
            const bool alreadyExists = factory->HasComponent(registry_, entity);

            ui::BeginDisabled(alreadyExists);
            if (ui::MenuItem(factory->GetName().c_str()))
                result = factory.get();
            ui::EndDisabled();

            if (result)
            {
                ui::CloseCurrentPopup();
                break;
            }
        }
        ui::EndPopup();
    }

    return result;
}

bool EntityManager::RenderExistingComponents(entt::entity entity)
{
    bool changed = false;
    for (const auto& factory : componentFactories_)
    {
        const IdScopeGuard guard{factory->GetName().c_str()};
        if (!factory->HasComponent(registry_, entity))
            continue;

        if (ui::Button(ICON_FA_TRASH_CAN "##RemoveComponent"))
        {
            ui_.pendingDestroyComponents_.emplace_back(entity, factory.get());
            changed = true;
        }
        if (ui::IsItemHovered())
            ui::SetTooltip("Remove this component from entity");
        ui::SameLine();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (factory->IsEmpty())
            flags |= ImGuiTreeNodeFlags_Bullet;

        if (ui::CollapsingHeader(factory->GetName().c_str(), flags))
        {
            ui::Indent();
            if (factory->RenderUI(registry_, entity))
            {
                ui_.pendingEditComponents_.push_back(factory.get());
                changed = true;
            }
            ui::Unindent();
        }
    }

    return changed;
}

void EntityManager::AddComponentType(ea::unique_ptr<EntityComponentFactory> factory)
{
    componentFactories_.push_back(ea::move(factory));
    componentTypesSorted_ = false;
}

EntityComponentFactory* EntityManager::FindComponentType(ea::string_view name) const
{
    for (const auto& factory : componentFactories_)
    {
        if (factory->GetName() == name)
            return factory.get();
    }
    return nullptr;
}

void EntityManager::CommitActions()
{
    if (!ui_.pendingMaterializations_.empty())
    {
        for (const auto& [entity, isMaterialized] : ui_.pendingMaterializations_)
        {
            if (isMaterialized)
                MaterializeEntity(entity);
            else
                DematerializeEntity(entity);
        }

        ui_.pendingMaterializations_.clear();
    }

    if (!ui_.pendingCreateComponents_.empty())
    {
        for (const auto& [entity, factory] : ui_.pendingCreateComponents_)
        {
            if (!registry_.valid(entity) || factory->HasComponent(registry_, entity))
            {
                URHO3D_LOGERROR("Cannot add component '{}' to entity {}", factory->GetName(), entity);
                continue;
            }

            factory->CreateComponent(registry_, entity);
        }

        ui_.pendingCreateComponents_.clear();
    }

    if (!ui_.pendingDestroyComponents_.empty())
    {
        for (const auto& [entity, factory] : ui_.pendingDestroyComponents_)
        {
            if (!registry_.valid(entity) || !factory->HasComponent(registry_, entity))
            {
                URHO3D_LOGERROR("Cannot remove component '{}' from entity {}", factory->GetName(), entity);
                continue;
            }

            factory->DestroyComponent(registry_, entity);
        }

        ui_.pendingDestroyComponents_.clear();
    }

    if (!ui_.pendingEditComponents_.empty())
    {
        for (const auto& factory : ui_.pendingEditComponents_)
            factory->CommitActions(registry_);

        ui_.pendingEditComponents_.clear();
    }
}

void EntityManager::OnComponentAdded(TrackedComponentBase* baseComponent)
{
    const auto entityReference = static_cast<EntityReference*>(baseComponent);
    entityReference->GetNode()->AddListener(entityReference);

    if (suppressComponentEvents_)
        return;

    pendingEntitiesAdded_.emplace(WeakPtr<EntityReference>(entityReference));
}

void EntityManager::OnComponentRemoved(TrackedComponentBase* baseComponent)
{
    if (suppressComponentEvents_)
        return;

    const auto entityReference = static_cast<EntityReference*>(baseComponent);
    pendingEntitiesAdded_.erase(WeakPtr<EntityReference>(entityReference));

    const entt::entity entity = entityReference->Entity();
    if (entity != entt::null)
    {
        URHO3D_ASSERT(registry_.valid(entity));
        registry_.destroy(entity);
    }
}

void EntityManager::OnAddedToScene(Scene* scene)
{
    SubscribeToEvent(scene, E_SCENEFORCEDPOSTUPDATE, &EntityManager::ForcedPostUpdate);
}

void EntityManager::OnRemovedFromScene()
{
    UnsubscribeFromEvent(E_SCENEFORCEDPOSTUPDATE);
}

void EntityManager::Synchronize()
{
    if (synchronizationInProgress_)
        return;
    synchronizationInProgress_ = true;

    for (EntityReference* entityReference : pendingEntitiesAdded_)
    {
        // If registry has spawned this entity, everything is already configured.
        const entt::entity entityHint = entityReference->Entity();
        if (EntityToReference(entityHint) == entityReference)
            continue;

        if (registry_.valid(entityHint) && EntityToReference(entityHint) == nullptr)
        {
            // If entity is known to the registry and is not yet connected, connect to it.
        }
        else
        {
            // New entity was added from the UI, create new entity.
            entityReference->SetEntityInternal(registry_.create(entityHint));
        }

        const entt::entity entity = entityReference->Entity();
        registry_.emplace<EntityMaterialized>(entity, WeakPtr<EntityReference>{entityReference});
        registry_.emplace_or_replace<MaterializationStatus>(entity, MaterializationStatus{true});
    }
    pendingEntitiesAdded_.clear();

    for (const auto& [entityReference, data] : pendingEntityDecodes_)
    {
        if (entityReference && entityReference->Entity() != entt::null)
            DecodeEntity(entityReference->Entity(), data);
    }
    pendingEntityDecodes_.clear();

    if (registryDirty_)
    {
        registryDirty_ = false;
        EnsureEntitiesMaterialized();
    }

    synchronizationInProgress_ = false;
}

bool EntityManager::IsEntityMaterialized(entt::entity entity) const
{
    URHO3D_ASSERT(registry_.valid(entity));
    return registry_.any_of<EntityMaterialized>(entity);
}

bool EntityManager::IsEntityValid(entt::entity entity) const
{
    return entity != entt::null && registry_.valid(entity);
}

EntityReference* EntityManager::EntityToReference(entt::entity entity) const
{
    const auto data = entity != entt::null ? registry_.try_get<EntityMaterialized>(entity) : nullptr;
    return data ? data->entityReference_.Get() : nullptr;
}

Node* EntityManager::EntityToNode(entt::entity entity) const
{
    const EntityReference* reference = EntityToReference(entity);
    return reference ? reference->GetNode() : nullptr;
}

entt::entity EntityManager::NodeToEntity(Node* node) const
{
    if (node)
    {
        if (auto entityReference = node->GetComponent<EntityReference>())
            return entityReference->Entity();
    }
    return entt::null;
}

Variant EntityManager::EntityToVariant(entt::entity entity)
{
    return Variant{static_cast<unsigned>(entity)};
}

entt::entity EntityManager::VariantToEntity(const Variant& variant)
{
    return static_cast<entt::entity>(variant.GetUInt());
}

void EntityManager::EnsureEntitiesMaterialized()
{
    for (const auto& [entity] : registry_.storage<entt::entity>().each())
    {
        const auto* status = registry_.try_get<MaterializationStatus>(entity);
        const auto* data = registry_.try_get<EntityMaterialized>(entity);
        if (!data && status && status->materialized_)
            MaterializeEntity(entity);
        else if (data && (!status || !status->materialized_))
            DematerializeEntity(entity);
    };
}

EntityReference* EntityManager::MaterializeEntity(entt::entity entity)
{
    if (EntityReference* existingEntityReference = EntityToReference(entity))
    {
        URHO3D_LOGWARNING("Entity {} is already materialized", entity);
        return existingEntityReference;
    }

    URHO3D_LOGTRACE("Entity {} is materializing", entity);

    Node* entityNode = entitiesContainer_->CreateChild("Entity");
    auto entityReference = MakeShared<EntityReference>(context_);
    entityReference->SetEntityInternal(entity);

    registry_.emplace_or_replace<EntityMaterialized>(entity, WeakPtr<EntityReference>{entityReference});
    registry_.emplace_or_replace<MaterializationStatus>(entity, MaterializationStatus{true});

    suppressComponentEvents_ = true;
    entityNode->AddComponent(entityReference, 0);
    suppressComponentEvents_ = false;

    OnEntityMaterialized(this, registry_, entity, entityReference);

    URHO3D_ASSERT(IsEntityMaterialized(entity));

    return entityReference;
}

void EntityManager::DematerializeEntity(entt::entity entity)
{
    if (!IsEntityMaterialized(entity))
    {
        URHO3D_LOGWARNING("Entity {} is already dematerialized", entity);
        return;
    }

    URHO3D_LOGTRACE("Entity {} is dematerializing", entity);

    EntityReference* entityReference = registry_.get<EntityMaterialized>(entity).entityReference_;
    URHO3D_ASSERT(entityReference);
    OnEntityDematerialized(this, registry_, entity, entityReference);

    FlattenEntityHierarchy(entityReference);
    entityReference->SetEntityInternal(entt::null);

    suppressComponentEvents_ = true;
    entityReference->GetNode()->Remove();
    suppressComponentEvents_ = false;

    registry_.remove<EntityMaterialized>(entity);
    registry_.emplace_or_replace<MaterializationStatus>(entity, MaterializationStatus{false});
}

ea::vector<entt::entity> EntityManager::GetEntities() const
{
    ea::vector<entt::entity> result;
    if (const auto* storage = registry_.storage<entt::entity>())
    {
        for (const auto& [entity] : storage->each())
            result.push_back(entity);
    }
    return result;
}

ByteVector EntityManager::EncodeEntity(entt::registry& registry, entt::entity entity)
{
    if (!registry.valid(entity))
    {
        URHO3D_LOGERROR("Cannot encode entity {}", entity);
        return {};
    }

    VectorBuffer buffer;
    BinaryOutputArchive archive{context_, buffer};
    SerializeStandaloneEntity(archive, registry, entity);
    return buffer.GetBuffer();
}

void EntityManager::DecodeEntity(entt::registry& registry, entt::entity entity, const ByteVector& data)
{
    if (!registry.valid(entity))
    {
        URHO3D_LOGERROR("Cannot decode entity {}", entity);
        return;
    }

    MemoryBuffer buffer{data};
    BinaryInputArchive archive{context_, buffer};
    SerializeStandaloneEntity(archive, registry, entity);
}

ByteVector EntityManager::EncodeEntity(entt::entity entity)
{
    return EncodeEntity(registry_, entity);
}

void EntityManager::DecodeEntity(entt::entity entity, const ByteVector& data)
{
    DecodeEntity(registry_, entity, data);
}

void EntityManager::QueueDecodeEntity(EntityReference* entityReference, const ByteVector& data)
{
    pendingEntityDecodes_.emplace_back(WeakPtr<EntityReference>{entityReference}, data);
}

void EntityManager::SetDataAttr(const ByteVector& data)
{
    MemoryBuffer buffer{data};
    BinaryInputArchive archive(context_, buffer);
    SerializeRegistry(archive);
    registryDirty_ = true;
}

ByteVector EntityManager::GetDataAttr() const
{
    VectorBuffer buffer;
    BinaryOutputArchive archive(context_, buffer);
    const_cast<EntityManager*>(this)->SerializeRegistry(archive);
    return buffer.GetBuffer();
}

void EntityManager::SerializeRegistry(Archive& archive)
{
    ea::vector<EntityMaterialized> entityReferences;

    if (archive.IsInput())
    {
        for (const auto& [_, data] : registry_.storage<EntityMaterialized>().each())
            entityReferences.push_back(data);

        registry_.clear();
    }

    ConsumeArchiveException(
        [&]
    {
        const auto block = archive.OpenUnorderedBlock("registry");
        SerializeEntities(archive);
        SerializeComponents<MaterializationStatus>(archive, "materializationStatus", registry_, 0);
        SerializeUserComponents(archive);
    });

    if (archive.IsInput())
    {
        for (const auto& data : entityReferences)
        {
            const entt::entity entity = data.entityReference_->Entity();
            if (registry_.valid(entity))
                registry_.emplace<EntityMaterialized>(entity, data);
        }
    }
}

void EntityManager::SerializeEntities(Archive& archive)
{
    const auto numEntities = static_cast<unsigned>(registry_.storage<entt::entity>().in_use());
    const auto block = archive.OpenArrayBlock("entities", numEntities);
    if (archive.IsInput())
    {
        for (unsigned i = 0; i < block.GetSizeHint(); ++i)
        {
            unsigned entityData = 0;
            archive.Serialize("entity", entityData);
            (void)registry_.create(static_cast<entt::entity>(entityData));
        }
    }
    else
    {
        static thread_local ea::vector<entt::entity> entitiesBuffer;
        auto& entities = entitiesBuffer;

        entities.clear();
        for (const auto& [entity] : registry_.storage<entt::entity>().each())
            entities.push_back(entity);
        ea::sort(entities.begin(), entities.end(), EntityIndexComparator{});

        for (const entt::entity entity : entities)
        {
            auto entityData = static_cast<unsigned>(entity);
            archive.Serialize("entity", entityData);
        };
    }
}

void EntityManager::SerializeUserComponents(Archive& archive)
{
    EnsureComponentTypesSorted();

    const auto storagesBlock = archive.OpenArrayBlock("storages", componentFactories_.size());

    if (archive.IsInput())
    {
        for (unsigned i = 0; i < storagesBlock.GetSizeHint(); ++i)
        {
            const auto storageBlock = archive.OpenSafeUnorderedBlock("storage");

            ea::string typeName;
            SerializeValue(archive, "type", typeName);

            unsigned version{};
            SerializeValue(archive, "version", version);

            if (const auto factory = FindComponentType(typeName))
                factory->SerializeComponents(archive, registry_, version);
        }
    }
    else
    {
        for (const auto& factory : componentFactories_)
        {
            const auto storageBlock = archive.OpenSafeUnorderedBlock("storage");

            ea::string typeName = factory->GetName();
            SerializeValue(archive, "type", typeName);

            unsigned version = factory->GetVersion();
            SerializeValue(archive, "version", version);

            factory->SerializeComponents(archive, registry_, version);
        }
    }
}

void EntityManager::SerializeStandaloneEntity(Archive& archive, entt::registry& registry, entt::entity entity)
{
    EnsureComponentTypesSorted();

    const auto storagesBlock = archive.OpenArrayBlock("components", componentFactories_.size());

    if (archive.IsInput())
    {
        for (unsigned i = 0; i < storagesBlock.GetSizeHint(); ++i)
        {
            const auto storageBlock = archive.OpenSafeUnorderedBlock("component");

            ea::string typeName;
            SerializeValue(archive, "_type", typeName);

            bool shouldExist{};
            SerializeValue(archive, "_exists", shouldExist);

            unsigned version{};
            SerializeValue(archive, "_version", version);

            if (const auto factory = FindComponentType(typeName))
            {
                const bool exists = factory->HasComponent(registry, entity);
                if (shouldExist)
                {
                    if (!exists)
                        factory->CreateComponent(registry, entity);
                    factory->SerializeComponent(archive, registry, entity, version);
                }
                else if (exists)
                {
                    factory->DestroyComponent(registry, entity);
                }
            }
        }
    }
    else
    {
        for (const auto& factory : componentFactories_)
        {
            const auto storageBlock = archive.OpenSafeUnorderedBlock("component");

            ea::string typeName = factory->GetName();
            SerializeValue(archive, "_type", typeName);

            bool exists = factory->HasComponent(registry, entity);
            SerializeValue(archive, "_exists", exists);

            unsigned version = factory->GetVersion();
            SerializeValue(archive, "_version", version);

            if (exists)
                factory->SerializeComponent(archive, registry, entity, version);
        }
    }
}

unsigned EntityManager::GetEntityVersion(entt::entity entity)
{
    return entt::to_version(entity);
}

unsigned EntityManager::GetEntityIndex(entt::entity entity)
{
    return static_cast<unsigned>(entt::to_entity(entity));
}

void EntityManager::ForcedPostUpdate()
{
    Synchronize();
    OnPostUpdateSynchronized(this, registry_);
}

} // namespace Urho3D
