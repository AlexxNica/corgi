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

#ifndef COMPONENT_LIBRARY_PHYSICS_H_
#define COMPONENT_LIBRARY_PHYSICS_H_

#include "btBulletDynamicsCommon.h"
#include "entity/component.h"
#include "event/event_manager.h"
#include "fplbase/asset_manager.h"
#include "flatbuffers/reflection.h"
#include "fplbase/renderer.h"
#include "fplbase/shader.h"
#include "library_components_generated.h"
#include "mathfu/glsl_mappings.h"

namespace fpl {
namespace component_library {

const int kMaxPhysicsBodies = 5;

// Data describing a single Bullet rigid body shape.
struct RigidBodyData {
  mathfu::vec3 offset;
  short collision_type;
  short collides_with;
  std::unique_ptr<btCollisionShape> shape;
  std::unique_ptr<btMotionState> motion_state;
  std::unique_ptr<btRigidBody> rigid_body;
  bool should_export;  // Whether the shape should be included on export.
};

// Data for scene object components.
struct PhysicsData {
 public:
  PhysicsData() : body_count(0), enabled(false) {}

  // The rigid bodies associated with the entity. Note that only the first one
  // can be set to not be kinematic, all subsequent ones are forced to be.
  RigidBodyData rigid_bodies[kMaxPhysicsBodies];
  int body_count;
  bool enabled;

  mathfu::vec3 Velocity() const {
    // Only the first body can be non-kinematic, and thus use velocity.
    const btVector3 vel = rigid_bodies[0].rigid_body->getLinearVelocity();
    return mathfu::vec3(vel.x(), vel.y(), vel.z());
  }
  void SetVelocity(mathfu::vec3 velocity) {
    const btVector3 vel(velocity.x(), velocity.y(), velocity.z());
    rigid_bodies[0].rigid_body->setLinearVelocity(vel);
  }
  mathfu::vec3 AngularVelocity() const {
    const btVector3 vel = rigid_bodies[0].rigid_body->getAngularVelocity();
    return mathfu::vec3(vel.x(), vel.y(), vel.z());
  }
  void SetAngularVelocity(mathfu::vec3 velocity) {
    const btVector3 vel(velocity.x(), velocity.y(), velocity.z());
    rigid_bodies[0].rigid_body->setAngularVelocity(vel);
  }
};

// Used by Bullet to render the physics scene as a wireframe.
class PhysicsDebugDrawer : public btIDebugDraw {
 public:
  virtual void drawLine(const btVector3& from, const btVector3& to,
                        const btVector3& color);
  virtual int getDebugMode() const { return DBG_DrawWireframe; }

  virtual void drawContactPoint(const btVector3& /*pointOnB*/,
                                const btVector3& /*normalOnB*/,
                                btScalar /*distance*/, int /*lifeTime*/,
                                const btVector3& /*color*/) {}
  virtual void reportErrorWarning(const char* /*warningString*/) {}
  virtual void draw3dText(const btVector3& /*location*/,
                          const char* /*textString*/) {}
  virtual void setDebugMode(int /*debugMode*/) {}

  Shader* shader() { return shader_; }
  void set_shader(Shader* shader) { shader_ = shader; }

  Renderer* renderer() { return renderer_; }
  void set_renderer(Renderer* renderer) { renderer_ = renderer; }

 private:
  Shader* shader_;
  Renderer* renderer_;
};

class PhysicsComponent : public entity::Component<PhysicsData> {
 public:
  PhysicsComponent() {}
  virtual ~PhysicsComponent();

  virtual void AddFromRawData(entity::EntityRef& entity, const void* raw_data);
  // TODO: Implement ExportRawData function for editor (b/21589546)
  virtual RawDataUniquePtr ExportRawData(entity::EntityRef& entity) const;

  virtual void Init();
  virtual void InitEntity(entity::EntityRef& /*entity*/);
  virtual void CleanupEntity(entity::EntityRef& entity);
  virtual void UpdateAllEntities(entity::WorldTime delta_time);

  void ProcessBulletTickCallback();

  void UpdatePhysicsFromTransform(entity::EntityRef& entity);

  void EnablePhysics(const entity::EntityRef& entity);
  void DisablePhysics(const entity::EntityRef& entity);

  // Generate an AABB based on the rendermesh that collides with the raycast
  // layer. Note, if the entity already collides with the raycast layer, no
  // change occurs. If there is no rendermesh, a unit cube is used instead.
  // Also sets whether the resulting shape should be exported.
  void GenerateRaycastShape(entity::EntityRef& entity, bool result_exportable);
  entity::EntityRef RaycastSingle(mathfu::vec3& start, mathfu::vec3& end);
  entity::EntityRef RaycastSingle(mathfu::vec3& start, mathfu::vec3& end,
                                  mathfu::vec3* hit_point);
  void DebugDrawWorld(Renderer* renderer, const mathfu::mat4& camera_transform);
  void DebugDrawObject(Renderer* renderer, const mathfu::mat4& camera_transform,
                       const entity::EntityRef& entity,
                       const mathfu::vec3& color);

  btDiscreteDynamicsWorld* bullet_world() { return bullet_world_.get(); }

  void set_gravity(float gravity) { gravity_ = gravity; }
  float gravity() const { return gravity_; }

  void set_max_steps(int max_steps) { max_steps_ = max_steps; }
  float max_steps() const { return max_steps_; }

 private:
  void UpdatePhysicsObjectsTransform(entity::EntityRef& entity,
                                     bool kinematic_only);

  event::EventManager* event_manager_;

  std::unique_ptr<btDiscreteDynamicsWorld> bullet_world_;
  std::unique_ptr<btBroadphaseInterface> broadphase_;
  std::unique_ptr<btDefaultCollisionConfiguration> collision_configuration_;
  std::unique_ptr<btCollisionDispatcher> collision_dispatcher_;
  std::unique_ptr<btSequentialImpulseConstraintSolver> constraint_solver_;

  PhysicsDebugDrawer debug_drawer_;

  float gravity_;
  int max_steps_;
};

}  // component_library
}  // fpl

FPL_ENTITY_REGISTER_COMPONENT(fpl::component_library::PhysicsComponent,
                              fpl::component_library::PhysicsData)

#endif  // COMPONENT_LIBRARY_PHYSICS_H_
