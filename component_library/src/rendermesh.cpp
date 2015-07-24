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

#include "component_library/rendermesh.h"
#include "component_library/common_services.h"
#include "component_library/transform.h"
#include "library_components_generated.h"
#include "fplbase/mesh.h"
#include "fplbase/utilities.h"

using mathfu::vec3;
using mathfu::mat4;

FPL_ENTITY_DEFINE_COMPONENT(fpl::component_library::RenderMeshComponent,
                            fpl::component_library::RenderMeshData)

namespace fpl {
namespace component_library {

// Offset the frustrum by this many world-units.  As long as no objects are
// larger than this number, they should still all draw, even if their
// registration points technically fall outside our frustrum.
static const float kFrustrumOffset = 50.0f;

void RenderMeshComponent::Init() {
  asset_manager_ =
      entity_manager_->GetComponent<CommonServicesComponent>()->asset_manager();
}

// Rendermesh depends on transform:
void RenderMeshComponent::InitEntity(entity::EntityRef& entity) {
  entity_manager_->AddEntityToComponent<TransformComponent>(entity);
}

void RenderMeshComponent::RenderPrep(const CameraInterface& camera) {
  for (int pass = 0; pass < RenderPass_kCount; pass++) {
    pass_render_list[pass].clear();
  }
  for (auto iter = component_data_.begin(); iter != component_data_.end();
       ++iter) {
    RenderMeshData* rendermesh_data = GetComponentData(iter->entity);
    TransformData* transform_data = Data<TransformData>(iter->entity);

    float max_cos = cos(camera.viewport_angle());
    vec3 camera_facing = camera.facing();
    vec3 camera_position = camera.position();

    // Put each entity into the list for each render pass it is
    // planning on participating in.
    for (int pass = 0; pass < RenderPass_kCount; pass++) {
      if (rendermesh_data->pass_mask & (1 << pass)) {
        if (rendermesh_data->currently_hidden) continue;
        if (!rendermesh_data->ignore_culling) {
          // Check to make sure objects are inside the frustrum of our
          // view-cone before we draw:
          vec3 entity_position =
              transform_data->world_transform.TranslationVector3D();
          vec3 pos_relative_to_camera = (entity_position - camera_position) +
                                        camera_facing * kFrustrumOffset;

          // Cache off the distance from the camera because we'll use it
          // later as a depth aproxamation.
          rendermesh_data->z_depth =
              (entity_position - camera.position()).LengthSquared();

          if (vec3::DotProduct(pos_relative_to_camera.Normalized(),
                               camera_facing.Normalized()) < max_cos) {
            // The origin point for this mesh is not in our field of view.  Cut
            // out early, and don't bother rendering it.
            continue;
          }
        }
        pass_render_list[pass].push_back(
            RenderlistEntry(iter->entity, &iter->data));
      }
    }
  }
  std::sort(pass_render_list[RenderPass_kOpaque].begin(),
            pass_render_list[RenderPass_kOpaque].end());
  std::sort(pass_render_list[RenderPass_kAlpha].begin(),
            pass_render_list[RenderPass_kAlpha].end(),
            std::greater<RenderlistEntry>());
}

void RenderMeshComponent::RenderAllEntities(Renderer& renderer,
                                            const CameraInterface& camera) {
  // Make sure we only draw the front-facing polygons:
  renderer.SetCulling(Renderer::kCullBack);

  // Render the actual game:
  for (int pass = 0; pass < RenderPass_kCount; pass++) {
    RenderPass(pass, camera, renderer);
  }
}

// Render a pass.
void RenderMeshComponent::RenderPass(int pass_id, const CameraInterface& camera,
                                     Renderer& renderer) {
  RenderPass(pass_id, camera, renderer, nullptr);
}

// Render a single render-pass, by ID.
void RenderMeshComponent::RenderPass(int pass_id, const CameraInterface& camera,
                                     Renderer& renderer,
                                     const Shader* shader_override) {
  mat4 camera_vp = camera.GetTransformMatrix();

  for (size_t i = 0; i < pass_render_list[pass_id].size(); i++) {
    entity::EntityRef& entity = pass_render_list[pass_id][i].entity;

    RenderMeshData* rendermesh_data = Data<RenderMeshData>(entity);

    TransformData* transform_data = Data<TransformData>(entity);

    mat4 world_transform = transform_data->world_transform;

    const mat4 mvp = camera_vp * world_transform;
    const mat4 world_matrix_inverse = world_transform.Inverse();

    renderer.camera_pos() = world_matrix_inverse * camera.position();
    renderer.light_pos() = world_matrix_inverse * light_position_;
    renderer.model_view_projection() = mvp;
    renderer.color() = rendermesh_data->tint;
    renderer.model() = world_transform;

    if (!shader_override && rendermesh_data->shader) {
      rendermesh_data->shader->Set(renderer);
    } else {
      shader_override->Set(renderer);
    }
    rendermesh_data->mesh->Render(renderer);
  }
}

void RenderMeshComponent::SetHiddenRecursively(const entity::EntityRef& entity,
                                               bool hidden) {
  RenderMeshData* rendermesh_data = Data<RenderMeshData>(entity);
  TransformData* transform_data = Data<TransformData>(entity);
  if (transform_data) {
    if (rendermesh_data) {
      rendermesh_data->currently_hidden = hidden;
    }
    for (auto iter = transform_data->children.begin();
         iter != transform_data->children.end(); ++iter) {
      SetHiddenRecursively(iter->owner, hidden);
    }
  }
}

void RenderMeshComponent::AddFromRawData(entity::EntityRef& entity,
                                         const void* raw_data) {
  auto rendermesh_def = static_cast<const RenderMeshDef*>(raw_data);

  // You need to call asset_manager before you can add from raw data,
  // otherwise it can't load up new meshes!
  assert(asset_manager_ != nullptr);
  assert(rendermesh_def->source_file() != nullptr);
  assert(rendermesh_def->shader() != nullptr);

  RenderMeshData* rendermesh_data = AddEntity(entity);

  rendermesh_data->mesh_filename = rendermesh_def->source_file()->c_str();
  rendermesh_data->shader_filename = rendermesh_def->shader()->c_str();
  rendermesh_data->ignore_culling = rendermesh_def->ignore_culling();

  rendermesh_data->mesh =
      asset_manager_->LoadMesh(rendermesh_def->source_file()->c_str());
  assert(rendermesh_data->mesh != nullptr);

  rendermesh_data->shader =
      asset_manager_->LoadShader(rendermesh_def->shader()->c_str());
  assert(rendermesh_data->shader != nullptr);
  rendermesh_data->ignore_culling = rendermesh_def->ignore_culling();

  rendermesh_data->default_hidden = rendermesh_def->hidden();
  rendermesh_data->currently_hidden = rendermesh_def->hidden();

  rendermesh_data->pass_mask = 0;
  if (rendermesh_def->render_pass() != nullptr) {
    for (size_t i = 0; i < rendermesh_def->render_pass()->size(); i++) {
      int render_pass = rendermesh_def->render_pass()->Get(i);
      assert(render_pass < RenderPass_kCount);
      rendermesh_data->pass_mask |= 1 << render_pass;
    }
  } else {
    // Anything unspecified is assumed to be opaque.
    rendermesh_data->pass_mask = (1 << RenderPass_kOpaque);
  }

  // TODO: Load this from a flatbuffer file instead of setting it.
  rendermesh_data->tint = mathfu::kOnes4f;
}

entity::ComponentInterface::RawDataUniquePtr RenderMeshComponent::ExportRawData(
    entity::EntityRef& entity) const {
  const RenderMeshData* data = GetComponentData(entity);
  if (data == nullptr) return nullptr;

  if (data->mesh_filename == "" || data->shader_filename == "") {
    // If we don't have a mesh filename or a shader, we can't be exported;
    // we were obviously created programatically.
    return nullptr;
  }

  flatbuffers::FlatBufferBuilder fbb;

  auto source_file =
      (data->mesh_filename != "") ? fbb.CreateString(data->mesh_filename) : 0;
  auto shader = (data->shader_filename != "")
                    ? fbb.CreateString(data->shader_filename)
                    : 0;
  std::vector<unsigned char> render_pass_vec;
  for (int i = 0; i < RenderPass_kCount; i++) {
    if (data->pass_mask & (1 << i)) {
      render_pass_vec.push_back(i);
    }
  }
  auto render_pass = fbb.CreateVector(render_pass_vec);

  RenderMeshDefBuilder builder(fbb);
  if (source_file.o != 0) {
    builder.add_source_file(source_file);
  }
  if (shader.o != 0) {
    builder.add_shader(shader);
  }
  if (render_pass.o != 0) {
    builder.add_render_pass(render_pass);
  }
  builder.add_ignore_culling(data->ignore_culling);

  fbb.Finish(builder.Finish());
  return fbb.ReleaseBufferPointer();
}

}  // component_library
}  // fpl
