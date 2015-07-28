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

#include <cfloat>
#include "component_library/physics.h"
#include "component_library/common_services.h"
#include "component_library/component_utils.h"
#include "component_library/rendermesh.h"
#include "component_library/transform.h"
#include "event/event_manager.h"
#include "events/collision.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/reflection.h"
#include "fplbase/flatbuffer_utils.h"
#include "fplbase/mesh.h"
#include "fplbase/utilities.h"
#include "mathfu/glsl_mappings.h"
#include "mathfu/vector.h"

using mathfu::vec3;
using mathfu::quat;

FPL_ENTITY_DEFINE_COMPONENT(fpl::component_library::PhysicsComponent,
                            fpl::component_library::PhysicsData)

namespace fpl {
namespace component_library {

// The function that is called from Bullet while calling World's stepSimulation.
// Note that it can be called multiple times per entity update, as Bullet can
// potentially update the world several times with that call.
static void BulletTickCallback(btDynamicsWorld* world, btScalar time_step);

static const char* kPhysicsShader = "shaders/color";

void PhysicsComponent::Init() {
  event_manager_ =
      entity_manager_->GetComponent<CommonServicesComponent>()->event_manager();
  AssetManager* asset_manager =
      entity_manager_->GetComponent<CommonServicesComponent>()->asset_manager();

  broadphase_.reset(new btDbvtBroadphase());
  collision_configuration_.reset(new btDefaultCollisionConfiguration());
  collision_dispatcher_.reset(
      new btCollisionDispatcher(collision_configuration_.get()));
  constraint_solver_.reset(new btSequentialImpulseConstraintSolver());
  bullet_world_.reset(new btDiscreteDynamicsWorld(
      collision_dispatcher_.get(), broadphase_.get(), constraint_solver_.get(),
      collision_configuration_.get()));
  bullet_world_->setGravity(btVector3(0.0f, 0.0f, gravity()));
  bullet_world_->setDebugDrawer(&debug_drawer_);
  bullet_world_->setInternalTickCallback(BulletTickCallback,
                                         static_cast<void*>(this));
  debug_drawer_.set_shader(asset_manager->LoadShader(kPhysicsShader));
}

PhysicsComponent::~PhysicsComponent() { ClearComponentData(); }

void PhysicsComponent::AddFromRawData(entity::EntityRef& entity,
                                      const void* raw_data) {
  auto physics_def = static_cast<const PhysicsDef*>(raw_data);
  PhysicsData* physics_data = AddEntity(entity);

  if (physics_def->shapes() && physics_def->shapes()->Length() > 0) {
    int shape_count = physics_def->shapes()->Length() > kMaxPhysicsBodies
                          ? kMaxPhysicsBodies
                          : physics_def->shapes()->Length();
    physics_data->body_count = shape_count;
    for (int index = 0; index < shape_count; ++index) {
      auto shape_def = physics_def->shapes()->Get(index);
      auto rb_data = &physics_data->rigid_bodies[index];
      switch (shape_def->data_type()) {
        case BulletShapeUnion_BulletSphereDef: {
          auto sphere_data =
              static_cast<const BulletSphereDef*>(shape_def->data());
          rb_data->shape.reset(new btSphereShape(sphere_data->radius()));
          break;
        }
        case BulletShapeUnion_BulletBoxDef: {
          auto box_data = static_cast<const BulletBoxDef*>(shape_def->data());
          btVector3 half_extents(box_data->half_extents()->x(),
                                 box_data->half_extents()->y(),
                                 box_data->half_extents()->z());
          rb_data->shape.reset(new btBoxShape(btVector3(half_extents)));
          break;
        }
        case BulletShapeUnion_BulletCylinderDef: {
          auto cylinder_data =
              static_cast<const BulletCylinderDef*>(shape_def->data());
          btVector3 half_extents(cylinder_data->half_extents()->x(),
                                 cylinder_data->half_extents()->y(),
                                 cylinder_data->half_extents()->z());
          rb_data->shape.reset(new btCylinderShape(half_extents));
          break;
        }
        case BulletShapeUnion_BulletCapsuleDef: {
          auto capsule_data =
              static_cast<const BulletCapsuleDef*>(shape_def->data());
          rb_data->shape.reset(new btCapsuleShape(capsule_data->radius(),
                                                  capsule_data->height()));
          break;
        }
        case BulletShapeUnion_BulletConeDef: {
          auto cone_data = static_cast<const BulletConeDef*>(shape_def->data());
          rb_data->shape.reset(
              new btConeShape(cone_data->radius(), cone_data->height()));
          break;
        }
        case BulletShapeUnion_BulletStaticPlaneDef: {
          auto plane_data =
              static_cast<const BulletStaticPlaneDef*>(shape_def->data());
          btVector3 normal(plane_data->normal()->x(), plane_data->normal()->y(),
                           plane_data->normal()->z());
          rb_data->shape.reset(
              new btStaticPlaneShape(normal, plane_data->constant()));
          break;
        }
        case BulletShapeUnion_BulletNoShapeDef:
        default: {
          rb_data->shape.reset(new btEmptyShape());
          break;
        }
      }
      rb_data->motion_state.reset(new btDefaultMotionState());
      btScalar mass = shape_def->mass();
      btVector3 inertia(0.0f, 0.0f, 0.0f);
      if (rb_data->shape->getShapeType() != EMPTY_SHAPE_PROXYTYPE) {
        rb_data->shape->calculateLocalInertia(mass, inertia);
      }
      btRigidBody::btRigidBodyConstructionInfo rigid_body_builder(
          mass, rb_data->motion_state.get(), rb_data->shape.get(), inertia);
      rigid_body_builder.m_restitution = shape_def->restitution();
      rb_data->rigid_body.reset(new btRigidBody(rigid_body_builder));
      rb_data->rigid_body->setUserIndex(entity.index());
      rb_data->rigid_body->setUserPointer(entity.container());

      // Only the first shape can be non-kinematic.
      if (index > 0 || physics_def->kinematic()) {
        rb_data->rigid_body->setCollisionFlags(
            rb_data->rigid_body->getCollisionFlags() |
            btCollisionObject::CF_KINEMATIC_OBJECT);
      }
      if (shape_def->offset()) {
        rb_data->offset = LoadVec3(shape_def->offset());
      } else {
        rb_data->offset = mathfu::kZeros3f;
      }
      rb_data->collision_type = static_cast<short>(shape_def->collision_type());
      rb_data->collides_with = 0;
      if (shape_def->collides_with()) {
        for (auto collides = shape_def->collides_with()->begin();
             collides != shape_def->collides_with()->end(); ++collides) {
          rb_data->collides_with |= static_cast<short>(*collides);
        }
      }
      if (shape_def->user_tag()) {
        rb_data->user_tag = shape_def->user_tag()->str();
      }
      rb_data->should_export = true;

      bullet_world_->addRigidBody(rb_data->rigid_body.get(),
                                  rb_data->collision_type,
                                  rb_data->collides_with);
    }
  }

  physics_data->enabled = true;
  UpdatePhysicsFromTransform(entity);
}

entity::ComponentInterface::RawDataUniquePtr PhysicsComponent::ExportRawData(
    const entity::EntityRef& entity) const {
  const PhysicsData* data = GetComponentData(entity);
  if (data == nullptr) return nullptr;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(entity_manager_->GetComponent<CommonServicesComponent>()
                        ->export_force_defaults());
  std::vector<flatbuffers::Offset<fpl::BulletShapeDef>> shape_vector;
  bool kinematic = true;
  if (data->body_count > 0) {
    kinematic = data->rigid_bodies[0].rigid_body->isKinematicObject();
    for (int index = 0; index < data->body_count; ++index) {
      const RigidBodyData& body = data->rigid_bodies[index];
      // Skip shapes that are set not to export.
      if (!body.should_export) {
        continue;
      }
      BulletShapeUnion shape_type = BulletShapeUnion_BulletNoShapeDef;
      flatbuffers::Offset<void> shape_data;
      switch (body.shape->getShapeType()) {
        case SPHERE_SHAPE_PROXYTYPE: {
          auto sphere = static_cast<const btSphereShape*>(body.shape.get());
          BulletSphereDefBuilder sphere_builder(fbb);
          sphere_builder.add_radius(sphere->getRadius());
          shape_type = BulletShapeUnion_BulletSphereDef;
          shape_data = sphere_builder.Finish().Union();
          break;
        }
        case BOX_SHAPE_PROXYTYPE: {
          auto box = static_cast<const btBoxShape*>(body.shape.get());
          BulletBoxDefBuilder box_builder(fbb);
          btVector3 bt_half_extents = box->getHalfExtentsWithMargin();
          Vec3 half_extents{bt_half_extents.x(), bt_half_extents.y(),
                            bt_half_extents.z()};
          box_builder.add_half_extents(&half_extents);
          shape_type = BulletShapeUnion_BulletBoxDef;
          shape_data = box_builder.Finish().Union();
          break;
        }
        case CYLINDER_SHAPE_PROXYTYPE: {
          auto cylinder = static_cast<const btCylinderShape*>(body.shape.get());
          BulletCylinderDefBuilder cylinder_builder(fbb);
          btVector3 bt_half_extents = cylinder->getHalfExtentsWithMargin();
          Vec3 half_extents{bt_half_extents.x(), bt_half_extents.y(),
                            bt_half_extents.z()};
          cylinder_builder.add_half_extents(&half_extents);
          shape_type = BulletShapeUnion_BulletCylinderDef;
          shape_data = cylinder_builder.Finish().Union();
          break;
        }
        case CAPSULE_SHAPE_PROXYTYPE: {
          auto capsule = static_cast<const btCapsuleShape*>(body.shape.get());
          BulletCapsuleDefBuilder capsule_builder(fbb);
          capsule_builder.add_radius(capsule->getRadius());
          capsule_builder.add_height(2.0f * capsule->getHalfHeight());
          shape_type = BulletShapeUnion_BulletCapsuleDef;
          shape_data = capsule_builder.Finish().Union();
          break;
        }
        case CONE_SHAPE_PROXYTYPE: {
          auto cone = static_cast<const btConeShape*>(body.shape.get());
          BulletConeDefBuilder cone_builder(fbb);
          cone_builder.add_radius(cone->getRadius());
          cone_builder.add_height(cone->getHeight());
          shape_type = BulletShapeUnion_BulletConeDef;
          shape_data = cone_builder.Finish().Union();
          break;
        }
        case STATIC_PLANE_PROXYTYPE: {
          auto plane = static_cast<const btStaticPlaneShape*>(body.shape.get());
          BulletStaticPlaneDefBuilder plane_builder(fbb);
          btVector3 bt_normal = plane->getPlaneNormal();
          Vec3 normal{bt_normal.x(), bt_normal.y(), bt_normal.z()};
          plane_builder.add_normal(&normal);
          plane_builder.add_constant(plane->getPlaneConstant());
          shape_type = BulletShapeUnion_BulletStaticPlaneDef;
          shape_data = plane_builder.Finish().Union();
          break;
        }
        case EMPTY_SHAPE_PROXYTYPE: {
          BulletNoShapeDefBuilder empty_builder(fbb);
          shape_type = BulletShapeUnion_BulletNoShapeDef;
          shape_data = empty_builder.Finish().Union();
          break;
        }
        default: { assert(0); }
      }

      std::vector<signed short> collides_with;
      for (signed short layer = 1;
           layer < static_cast<signed short>(BulletCollisionType_End);
           layer = layer << 1) {
        if (body.collides_with & layer) {
          collides_with.push_back(layer);
        }
      }
      auto collides = fbb.CreateVector(collides_with);
      auto user_tag = fbb.CreateString(body.user_tag);

      BulletShapeDefBuilder shape_builder(fbb);
      shape_builder.add_data_type(shape_type);
      shape_builder.add_data(shape_data);
      float invMass = body.rigid_body->getInvMass();
      shape_builder.add_mass(invMass ? 1.0f / invMass : 0.0f);
      shape_builder.add_restitution(body.rigid_body->getRestitution());
      fpl::Vec3 offset{body.offset.x(), body.offset.y(), body.offset.z()};
      shape_builder.add_offset(&offset);
      shape_builder.add_collision_type(
          static_cast<BulletCollisionType>(body.collision_type));
      shape_builder.add_collides_with(collides);
      shape_builder.add_user_tag(user_tag);
      shape_vector.push_back(shape_builder.Finish());
    }
  }
  // If no shapes were exported, there is nothing to be saved, as the
  // additional flags all reflect information about the saved shapes.
  if (!shape_vector.size()) {
    return nullptr;
  }

  auto shapes = fbb.CreateVector(shape_vector);
  PhysicsDefBuilder builder(fbb);
  builder.add_kinematic(kinematic);
  builder.add_shapes(shapes);

  fbb.Finish(builder.Finish());
  return fbb.ReleaseBufferPointer();
}

void PhysicsComponent::UpdateAllEntities(entity::WorldTime delta_time) {
  // Step the world.
  bullet_world_->stepSimulation(delta_time / 1000.f, max_steps());

  // Copy position information to Transforms.
  for (auto iter = component_data_.begin(); iter != component_data_.end();
       ++iter) {
    PhysicsData* physics_data = Data<PhysicsData>(iter->entity);
    TransformData* transform_data = Data<TransformData>(iter->entity);

    if (physics_data->body_count == 0 || !physics_data->enabled) {
      continue;
    }
    if (!physics_data->rigid_bodies[0].rigid_body->isKinematicObject()) {
      auto trans =
          physics_data->rigid_bodies[0].rigid_body->getWorldTransform();
      // The quaternion provided by Bullet is using a right-handed coordinate
      // system, while mathfu assumes left. Thus the axes need to be negated.
      // It also needs to be normalized, as the provided value is not.
      transform_data->orientation = mathfu::quat(
          trans.getRotation().getW(), -trans.getRotation().getX(),
          -trans.getRotation().getY(), -trans.getRotation().getZ());
      transform_data->orientation.Normalize();

      vec3 offset = transform_data->orientation.Inverse() *
                    physics_data->rigid_bodies[0].offset;
      transform_data->position =
          mathfu::vec3(trans.getOrigin().getX(), trans.getOrigin().getY(),
                       trans.getOrigin().getZ()) -
          offset;
    }
    // Update any kinematic objects with the current transform.
    UpdatePhysicsObjectsTransform(iter->entity, true);
  }
}

static void BulletTickCallback(btDynamicsWorld* world,
                               btScalar /* time_step */) {
  PhysicsComponent* pc =
      static_cast<PhysicsComponent*>(world->getWorldUserInfo());
  pc->ProcessBulletTickCallback();
}

void PhysicsComponent::ProcessBulletTickCallback() {
  // Check for collisions. Note that the number of manifolds and contacts might
  // change when resolving collisions, so the result should not be cached.
  for (int manifold_index = 0;
       manifold_index < collision_dispatcher_->getNumManifolds();
       manifold_index++) {
    btPersistentManifold* contact_manifold =
        collision_dispatcher_->getManifoldByIndexInternal(manifold_index);

    for (int contact_index = 0;
         contact_index < contact_manifold->getNumContacts(); contact_index++) {
      btManifoldPoint& pt = contact_manifold->getContactPoint(contact_index);
      if (pt.getDistance() < 0.0f) {
        auto body_a = contact_manifold->getBody0();
        auto body_b = contact_manifold->getBody1();
        auto container_a =
            static_cast<VectorPool<entity::Entity>*>(body_a->getUserPointer());
        auto container_b =
            static_cast<VectorPool<entity::Entity>*>(body_b->getUserPointer());
        // Only generate events if both containers were defined
        if (container_a == nullptr || container_b == nullptr) {
          continue;
        }

        entity::EntityRef entity_a(container_a, body_a->getUserIndex());
        entity::EntityRef entity_b(container_b, body_b->getUserIndex());
        vec3 position_a(pt.getPositionWorldOnA().x(),
                        pt.getPositionWorldOnA().y(),
                        pt.getPositionWorldOnA().z());
        vec3 position_b(pt.getPositionWorldOnB().x(),
                        pt.getPositionWorldOnB().y(),
                        pt.getPositionWorldOnB().z());
        std::string tag_a;
        std::string tag_b;
        auto physics_a = Data<PhysicsData>(entity_a);
        for (int i = 0; i < physics_a->body_count; i++) {
          if (physics_a->rigid_bodies[i].rigid_body.get() == body_a) {
            tag_a = physics_a->rigid_bodies[i].user_tag;
          }
        }
        auto physics_b = Data<PhysicsData>(entity_b);
        for (int i = 0; i < physics_b->body_count; i++) {
          if (physics_b->rigid_bodies[i].rigid_body.get() == body_b) {
            tag_b = physics_b->rigid_bodies[i].user_tag;
          }
        }

        event_manager_->BroadcastEvent(CollisionPayload(
            entity_a, position_a, tag_a, entity_b, position_b, tag_b));
      }
    }
  }
}

// Physics component requires that you have a transform component:
void PhysicsComponent::InitEntity(entity::EntityRef& entity) {
  entity_manager_->AddEntityToComponent<TransformComponent>(entity);
}

void PhysicsComponent::CleanupEntity(entity::EntityRef& entity) {
  DisablePhysics(entity);
}

void PhysicsComponent::EnablePhysics(const entity::EntityRef& entity) {
  PhysicsData* physics_data = Data<PhysicsData>(entity);
  if (!physics_data->enabled) {
    physics_data->enabled = true;
    for (int i = 0; i < physics_data->body_count; i++) {
      auto rb_data = &physics_data->rigid_bodies[i];
      bullet_world_->addRigidBody(rb_data->rigid_body.get(),
                                  rb_data->collision_type,
                                  rb_data->collides_with);
    }
  }
}

void PhysicsComponent::DisablePhysics(const entity::EntityRef& entity) {
  PhysicsData* physics_data = Data<PhysicsData>(entity);
  if (physics_data->enabled) {
    physics_data->enabled = false;
    for (int i = 0; i < physics_data->body_count; i++) {
      auto rb_data = &physics_data->rigid_bodies[i];
      bullet_world_->removeRigidBody(rb_data->rigid_body.get());
    }
  }
}

void PhysicsComponent::UpdatePhysicsFromTransform(entity::EntityRef& entity) {
  // Update all objects on the entity, not just kinematic ones.
  UpdatePhysicsObjectsTransform(entity, false);
}

void PhysicsComponent::UpdatePhysicsObjectsTransform(entity::EntityRef& entity,
                                                     bool kinematic_only) {
  if (Data<PhysicsData>(entity) == nullptr) return;

  PhysicsData* physics_data = Data<PhysicsData>(entity);
  TransformData* transform_data = Data<TransformData>(entity);

  // Bullet assumes a right handed system, while mathfu is left, so the axes
  // need to be negated.
  btQuaternion orientation(-transform_data->orientation.vector().x(),
                           -transform_data->orientation.vector().y(),
                           -transform_data->orientation.vector().z(),
                           transform_data->orientation.scalar());

  for (int i = 0; i < physics_data->body_count; i++) {
    auto rb_data = &physics_data->rigid_bodies[i];
    if (kinematic_only && !rb_data->rigid_body->isKinematicObject()) {
      continue;
    }
    vec3 offset = transform_data->orientation.Inverse() * rb_data->offset;
    btVector3 position(transform_data->position.x() + offset.x(),
                       transform_data->position.y() + offset.y(),
                       transform_data->position.z() + offset.z());
    btTransform transform(orientation, position);
    rb_data->rigid_body->setWorldTransform(transform);
    rb_data->motion_state->setWorldTransform(transform);
  }
}

entity::EntityRef PhysicsComponent::RaycastSingle(mathfu::vec3& start,
                                                  mathfu::vec3& end) {
  return RaycastSingle(start, end, BulletCollisionType_Raycast, nullptr);
}

entity::EntityRef PhysicsComponent::RaycastSingle(mathfu::vec3& start,
                                                  mathfu::vec3& end,
                                                  short layer_mask) {
  return RaycastSingle(start, end, layer_mask, nullptr);
}

entity::EntityRef PhysicsComponent::RaycastSingle(mathfu::vec3& start,
                                                  mathfu::vec3& end,
                                                  mathfu::vec3* hit_point) {
  return RaycastSingle(start, end, BulletCollisionType_Raycast, hit_point);
}

entity::EntityRef PhysicsComponent::RaycastSingle(mathfu::vec3& start,
                                                  mathfu::vec3& end,
                                                  short layer_mask,
                                                  mathfu::vec3* hit_point) {
  btVector3 bt_start = btVector3(start.x(), start.y(), start.z());
  btVector3 bt_end = btVector3(end.x(), end.y(), end.z());
  btCollisionWorld::ClosestRayResultCallback ray_results(bt_start, bt_end);
  ray_results.m_collisionFilterGroup = layer_mask;

  bullet_world_->rayTest(bt_start, bt_end, ray_results);
  if (ray_results.hasHit()) {
    auto container = static_cast<VectorPool<entity::Entity>*>(
        ray_results.m_collisionObject->getUserPointer());
    if (container != nullptr) {
      if (hit_point != nullptr)
        *hit_point = vec3(ray_results.m_hitPointWorld.x(),
                          ray_results.m_hitPointWorld.y(),
                          ray_results.m_hitPointWorld.z());

      return entity::EntityRef(container,
                               ray_results.m_collisionObject->getUserIndex());
    }
  }
  return entity::EntityRef();
}

void PhysicsComponent::GenerateRaycastShape(entity::EntityRef& entity,
                                            bool result_exportable) {
  PhysicsData* data = GetComponentData(entity);
  if (data->body_count == kMaxPhysicsBodies) {
    return;
  }
  // If the entity is already raycastable, there isn't a need to do anything
  for (int index = 0; index < data->body_count; ++index) {
    auto shape = &data->rigid_bodies[index];
    if (shape->collides_with & BulletCollisionType_Raycast) {
      return;
    }
  }
  // Add an AABB about the entity for raycasting purposes
  vec3 max(-FLT_MAX);
  vec3 min(FLT_MAX);
  if (!GetMaxMinPositionsForEntity(entity, *entity_manager_, &max, &min)) {
    max = min = mathfu::kZeros3f;
  }
  auto rb_data = &data->rigid_bodies[data->body_count++];
  auto transform_data = Data<TransformData>(entity);
  // Make sure it is at least one unit in each direction
  vec3 extents = vec3::Max(max - min, mathfu::kOnes3f);
  btVector3 bt_extents(extents.x(), extents.y(), extents.z());
  rb_data->offset = (max + min) / 2.0f;
  rb_data->shape.reset(new btBoxShape(bt_extents / 2.0f));
  vec3 transformed_offset =
      transform_data->orientation.Inverse() * rb_data->offset;
  btVector3 position(transform_data->position.x() + transformed_offset.x(),
                     transform_data->position.y() + transformed_offset.y(),
                     transform_data->position.z() + transformed_offset.z());
  btQuaternion orientation(-transform_data->orientation.vector().x(),
                           -transform_data->orientation.vector().y(),
                           -transform_data->orientation.vector().z(),
                           transform_data->orientation.scalar());
  rb_data->motion_state.reset(
      new btDefaultMotionState(btTransform(orientation, position)));
  btRigidBody::btRigidBodyConstructionInfo rigid_body_builder(
      0, rb_data->motion_state.get(), rb_data->shape.get(), btVector3());
  rb_data->rigid_body.reset(new btRigidBody(rigid_body_builder));
  rb_data->rigid_body->setUserIndex(entity.index());
  rb_data->rigid_body->setUserPointer(entity.container());
  rb_data->rigid_body->setCollisionFlags(
      rb_data->rigid_body->getCollisionFlags() |
      btCollisionObject::CF_KINEMATIC_OBJECT);
  rb_data->collision_type = BulletCollisionType_Raycast;
  rb_data->collides_with = BulletCollisionType_Raycast;
  rb_data->should_export = result_exportable;
  bullet_world_->addRigidBody(rb_data->rigid_body.get(),
                              rb_data->collision_type, rb_data->collides_with);
  data->enabled = true;
}

void PhysicsComponent::DebugDrawWorld(Renderer* renderer,
                                      const mathfu::mat4& camera_transform) {
  renderer->model_view_projection() = camera_transform;
  debug_drawer_.set_renderer(renderer);
  bullet_world_->debugDrawWorld();
}

void PhysicsComponent::DebugDrawObject(Renderer* renderer,
                                       const mathfu::mat4& camera_transform,
                                       const entity::EntityRef& entity,
                                       const mathfu::vec3& color) {
  auto physics_data = Data<PhysicsData>(entity);
  if (physics_data == nullptr) {
    return;
  }
  renderer->model_view_projection() = camera_transform;
  debug_drawer_.set_renderer(renderer);
  for (int i = 0; i < physics_data->body_count; i++) {
    auto rb_data = &physics_data->rigid_bodies[i];
    bullet_world_->debugDrawObject(rb_data->rigid_body->getWorldTransform(),
                                   rb_data->shape.get(),
                                   btVector3(color.x(), color.y(), color.z()));
  }
}

void PhysicsDebugDrawer::drawLine(const btVector3& from, const btVector3& to,
                                  const btVector3& color) {
  if (renderer_ != nullptr) {
    renderer_->color() = vec4(color.x(), color.y(), color.z(), 1.0f);
    if (shader_ != nullptr) {
      shader_->Set(*renderer_);
    }
  }

  static const Attribute attributes[] = {kPosition3f, kEND};
  static const unsigned short indices[] = {0, 1};
  const btVector3 vertices[] = {from, to};
  Mesh::RenderArray(Mesh::kLines, 2, attributes, sizeof(btVector3),
                    reinterpret_cast<const char*>(vertices), indices);
}

}  // component_library
}  // fpl
