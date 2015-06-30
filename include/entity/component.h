// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef FPL_COMPONENT_H_
#define FPL_COMPONENT_H_

#include "entity/component_id_lookup.h"
#include "entity/component_interface.h"
#include "entity/entity.h"
#include "entity/entity_common.h"
#include "entity/entity_manager.h"
#include "entity/vector_pool.h"

namespace fpl {
namespace entity {

// Component class.
// All components should should extend this class.  The type T is used to
// specify the structure of the data that needs to be associated with each
// entity.
template <typename T>
class Component : public ComponentInterface {
 public:
  // Structure associated with each entity.
  // Contains the template struct, as well as a pointer back to the
  // entity that owns this data.
  struct ComponentData {
    EntityRef entity;
    T data;
  };
  typedef typename VectorPool<ComponentData>::Iterator EntityIterator;

  Component() : entity_manager_(nullptr) {}

  virtual ~Component() {}

  // AddEntity is a much better function, but we can't have it in the
  // interface class, since it needs to know about type T for the return
  // value.  This provides an alternate way to add things if you don't
  // care about the returned data structure, and you don't feel like
  // casting the BaseComponent into something more specific.
  virtual void AddEntityGenerically(EntityRef& entity) { AddEntity(entity); }

  // Adds an entity to the list of things this component is tracking.
  // Returns the data structure associated with the component.
  // Note that if we're already registered for this component, this
  // will just return a reference to the existing data and not change anything.
  T* AddEntity(EntityRef& entity, AllocationLocation alloc_location) {
    if (entity->IsRegisteredForComponent(GetComponentId())) {
      return GetComponentData(entity);
    }
    // No existing data, so we allocate some and return it:
    size_t index = component_data_.GetNewElement(alloc_location).index();
    entity->SetComponentDataIndex(GetComponentId(), index);
    ComponentData* component_data = component_data_.GetElementData(index);
    component_data->entity = entity;
    InitEntity(entity);
    return &(component_data->data);
  }

  T* AddEntity(EntityRef entity) { return AddEntity(entity, kAddToBack); }

  // Removes an entity from our list of entities, marks the entity as not using
  // this component anymore, calls the destructor on the data, and returns
  // the memory to the memory pool.
  virtual void RemoveEntity(EntityRef& entity) {
    RemoveEntityInternal(entity);
    component_data_.FreeElement(GetComponentDataIndex(entity));
    entity->SetComponentDataIndex(GetComponentId(), kUnusedComponentIndex);
  }

  // Same as RemoveEntity() above, but returns an iterator to the entity after
  // the one we've just removed.
  virtual EntityIterator RemoveEntity(EntityIterator iter) {
    EntityRef entity = iter->entity;
    RemoveEntityInternal(entity);
    EntityIterator new_iter = component_data_.FreeElement(iter);
    entity->SetComponentDataIndex(GetComponentId(), kUnusedComponentIndex);
    return new_iter;
  }

  // Gets an iterator that will iterate over every entity in the component.
  virtual EntityIterator begin() { return component_data_.begin(); }

  // Gets an iterator which points to the end of the list of all entities in the
  // component.
  virtual EntityIterator end() { return component_data_.end(); }

  // Updates all entities.  Normally called by EntityManager, once per frame.
  virtual void UpdateAllEntities(WorldTime /*delta_time*/) {}

  // Returns the data for an entity as a void pointer.  The calling function
  // is expected to know what to do with it.
  // Returns null if the data does not exist.
  virtual void* GetComponentDataAsVoid(const EntityRef& entity) {
    return GetComponentData(entity);
  }
  virtual const void* GetComponentDataAsVoid(const EntityRef& entity) const {
    return GetComponentData(entity);
  }

  // Return the data we have stored at a given index.
  // Returns null if data_index indicates this component isn't present.
  T* GetComponentData(size_t data_index) {
    if (data_index == kUnusedComponentIndex) {
      return nullptr;
    }
    ComponentData* element_data = component_data_.GetElementData(data_index);
    return (element_data != nullptr) ? &(element_data->data) : nullptr;
  }

  // Return the data we have stored at a given index.
  // Returns null if data_index indicates this component isn't present.
  T* GetComponentData(const EntityRef& entity) {
    size_t data_index = GetComponentDataIndex(entity);
    if (data_index >= component_data_.Size()) {
      return nullptr;
    }
    return GetComponentData(data_index);
  }

  // Return the data we have stored at a given index.
  // Returns null if the data does not exist.
  // WARNING: This pointer is NOT stable in memory.  Calls to AddEntity and
  // AddEntityGenerically may force the storage class to resize,
  // shuffling around the location of this data.
  const T* GetComponentData(size_t data_index) const {
    return const_cast<Component*>(this)->GetComponentData(data_index);
  }

  // Return our data for a given entity.
  // Returns null if the data does not exist.
  // WARNING: This pointer is NOT stable in memory.  Calls to AddEntity and
  // AddEntityGenerically may force the storage class to resize,
  // shuffling around the location of this data.
  const T* GetComponentData(const EntityRef& entity) const {
    return const_cast<Component*>(this)->GetComponentData(entity);
  }

  // Clears all tracked component data.
  void virtual ClearComponentData() {
    for (auto iter = component_data_.begin(); iter != component_data_.end();
         iter = RemoveEntity(iter)) {
    }
  }

  // Utility function for getting the component data for a specific component.
  template <typename ComponentDataType>
  ComponentDataType* Data(const EntityRef& entity) {
    return entity_manager_->GetComponentData<ComponentDataType>(entity);
  }

  // Utility function for getting the component object for a specific component.
  template <typename ComponentDataType>
  ComponentDataType* GetComponent() {
    return static_cast<ComponentDataType*>(entity_manager_->GetComponent(
        ComponentIdLookup<ComponentDataType>::kComponentId));
  }

  // Virtual methods we inherited from component_interface:

  // Override this with any code that we want to execute when the component
  // is added to the entity manager.  (i. e. once, at the beginning of the
  // game, before any entities are added.)
  virtual void Init() {}

  // Override this with code that we want to execute when an entity is added.
  virtual void InitEntity(EntityRef& /*entity*/) {}

  // By default, components don't support this functionality. If you do
  // support it, override this to return raw data that you can read back later.
  virtual RawDataUniquePtr ExportRawData(EntityRef& /*unused*/) const {
    return nullptr;
  }

  // By default, components don't support populating their raw data into a
  // buffer. If you do support it, override this to populate raw data via the
  // method of your choosing.
  virtual void* PopulateRawData(EntityRef& /*unused*/, void* /*unused*/) const {
    return nullptr;
  }

  // Override this with code that executes when this component is removed from
  // the entity manager.  (i. e. usually when the game/state is over and
  // everythingis shutting down.)
  virtual void Cleanup() {}

  // Override this with any code that needs to execute when an entity is
  // removed from this component.
  virtual void CleanupEntity(EntityRef& /*entity*/) {}

  // Set the entity manager for this component.  (used as the main point of
  // contact for components that need to talk to other things.)
  virtual void SetEntityManager(EntityManager* entity_manager) {
    entity_manager_ = entity_manager;
  }

  // Returns the ID of this component.
  static ComponentId GetComponentId() {
    return ComponentIdLookup<T>::kComponentId;
  }

 private:
  void RemoveEntityInternal(EntityRef& entity) {
    // Allow components to handle any per-entity cleanup that it needs to do.
    CleanupEntity(entity);
  }

 protected:
  size_t GetComponentDataIndex(const EntityRef& entity) const {
    return entity->GetComponentDataIndex(GetComponentId());
  }

  VectorPool<ComponentData> component_data_;
  EntityManager* entity_manager_;
};

}  // entity
}  // fpl

#endif  // FPL_COMPONENT_H_
