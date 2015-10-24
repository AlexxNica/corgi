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

#ifndef DEFAULT_ENTITY_FACTORY_H_
#define DEFAULT_ENTITY_FACTORY_H_

#include <set>
#include <vector>
#include <string>
#include "component_library/entity_factory.h"
#include "component_library/meta.h"
#include "entity/entity_manager.h"
#include "flatbuffers/flatbuffers.h"
#include "fplbase/utilities.h"

namespace fpl {
namespace component_library {

class DefaultEntityFactory : public component_library::EntityFactory {
 public:
  virtual bool ReadEntityList(const void* entity_list,
                              std::vector<const void*>* entity_defs);
  virtual bool ReadEntityDefinition(const void* entity_definition,
                                    std::vector<const void*>* component_defs);

  virtual bool CreatePrototypeRequest(const char* prototype_name,
                                      std::vector<uint8_t>* request);

  virtual bool CreateEntityDefinition(
      const std::vector<const void*>& component_data,
      std::vector<uint8_t>* entity_definition);

  virtual bool CreateEntityList(const std::vector<const void*>& entity_defs,
                                std::vector<uint8_t>* entity_list);
};

}  // namespace component_library
}  // namespace fpl

#endif  // DEFAULT_ENTITY_FACTORY_H_
