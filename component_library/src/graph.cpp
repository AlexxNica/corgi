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

#include "component_library/graph.h"
#include <cstdio>
#include "breadboard/graph.h"
#include "breadboard/graph_factory.h"
#include "breadboard/graph_state.h"
#include "component_library/common_services.h"
#include "library_components_generated.h"

FPL_ENTITY_DEFINE_COMPONENT(fpl::component_library::GraphComponent,
                            fpl::component_library::GraphData)

namespace fpl {
namespace component_library {

breadboard::NodeEventBroadcaster* GraphComponent::GetCreateBroadcaster(
    entity::EntityRef entity) {
  auto graph_data = Data<GraphData>(entity);
  if (!graph_data) {
    graph_data = AddEntity(entity);
  }
  return &graph_data->broadcaster;
}

void GraphComponent::Init() {
  auto common_services_component =
      entity_manager_->GetComponent<CommonServicesComponent>();
  graph_factory_ = common_services_component->graph_factory();
}

void GraphComponent::AddFromRawData(entity::EntityRef& entity,
                                    const void* raw_data) {
  GraphData* graph_data = AddEntity(entity);
  auto graph_def = static_cast<const GraphDef*>(raw_data);
  auto filename_list = graph_def->filename_list();
  graph_data->graphs.clear();
  if (filename_list) {
    for (size_t i = 0; i < filename_list->size(); ++i) {
      auto filename = filename_list->Get(i);
      graph_data->graphs.push_back(SerializableGraphState());
      graph_data->graphs.back().graph_state.reset(new breadboard::GraphState);
      graph_data->graphs.back().filename = filename->c_str();
    }
  }
}

void GraphComponent::EntityPostLoadFixup(entity::EntityRef& entity) {
  graph_entity_ = entity;
  GraphData* graph_data = Data<GraphData>(entity);
  if (graph_data) {
    for (auto graph_iter = graph_data->graphs.begin();
         graph_iter != graph_data->graphs.end(); ++graph_iter) {
      breadboard::Graph* graph =
          graph_factory_->LoadGraph(graph_iter->filename.c_str());
      if (graph) {
        graph_iter->graph_state->Initialize(graph);
      }
    }
  }
}

void GraphComponent::PostLoadFixup() {
  for (auto iter = component_data_.begin(); iter != component_data_.end();
       ++iter) {
    EntityPostLoadFixup(iter->entity);
  }
}

entity::ComponentInterface::RawDataUniquePtr GraphComponent::ExportRawData(
    const entity::EntityRef& entity) const {
  const GraphData* data = GetComponentData(entity);
  if (data == nullptr) return nullptr;

  flatbuffers::FlatBufferBuilder fbb;
  std::vector<flatbuffers::Offset<flatbuffers::String>> filenames_vector;
  for (auto iter = data->graphs.begin(); iter != data->graphs.end(); ++iter) {
    filenames_vector.push_back(fbb.CreateString(iter->filename));
  }
  GraphDefBuilder builder(fbb);
  if (filenames_vector.size() > 0) {
    builder.add_filename_list(fbb.CreateVector(filenames_vector));
  }
  fbb.Finish(builder.Finish());
  return fbb.ReleaseBufferPointer();
}

}  // fpl_project
}  // fpl
