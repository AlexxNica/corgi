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

#ifndef FPL_BASE_COMPONENT_H_
#define FPL_BASE_COMPONENT_H_

#include <stdint.h>
#include <memory>
#include <functional>
#include "entity/entity.h"
#include "entity/entity_common.h"
#include "entity/entity_manager.h"
#include "entity/vector_pool.h"

namespace fpl {
namespace entity {

class EntityManager;

typedef VectorPool<entity::Entity>::VectorPoolReference EntityRef;

// Basic component functionality.  All components implement this, and it's the
// minimum set of things you can do with a component even if you don't know what
// type it is.
class ComponentInterface {
 public:
  // Pointer type for exported raw data.
  typedef std::unique_ptr<uint8_t, std::function<void(uint8_t*)>>
      RawDataUniquePtr;

  virtual ~ComponentInterface() {}

  // Add an entity to the component.  (Note - usually you'll want to use
  // Component::AddEntity, since that gives you back a pointer to the data
  // assigned to the component.)
  virtual void AddEntityGenerically(EntityRef& entity) = 0;

  // Remove an entity from the component's list.
  virtual void RemoveEntity(EntityRef& entity) = 0;

  // Update all entities that contain this component.
  virtual void UpdateAllEntities(WorldTime delta_time) = 0;

  // Clear all component data, effectively disassociating this component
  // from any entities.  (Note that this does NOT change entities, so they may
  // still think we have data for them.)  Normally this isn't something you
  // want to invoke directly.  It's usually used by things like Entity.Clear(),
  // when something wants to nuke all the data everywhere and start over.
  virtual void ClearComponentData() = 0;

  // Return the component data as a void* pointer.  (The caller is responsible
  // for casting it into something useful.)
  // WARNING: This pointer is NOT stable in memory.  Calls to
  // AddEntityGenerically may force the storage class to resize, shuffling
  // around the location of this data.
  virtual void* GetComponentDataAsVoid(const EntityRef&) = 0;
  virtual const void* GetComponentDataAsVoid(const EntityRef&) const = 0;

  // Called just after addition to the entitymanager
  virtual void Init() = 0;

  // Called just after an entity is added to this component.
  virtual void InitEntity(EntityRef& entity) = 0;

  // Used to build entities from data.  All components need to implement one.
  virtual void AddFromRawData(EntityRef& entity, const void* data) = 0;

  // Export current state to raw data to be later reimported via AddFromRawData.
  // If you don't support this functionality, return nullptr here.
  virtual RawDataUniquePtr ExportRawData(EntityRef& entity) const = 0;

  // Called just before removal from the entitymanager.
  virtual void Cleanup() = 0;

  // Called when the entity is removed from the manager.
  virtual void CleanupEntity(EntityRef& entity) = 0;

  // Set the entity manager for this component.  (used as the main point of
  // contact for components that need to talk to other things.)
  // (Normally assigned by entitymanager)
  virtual void SetEntityManager(EntityManager* entity_manager) = 0;
};

}  // entity
}  // fpl

#endif  // FPL_BASE_COMPONENT_H_
