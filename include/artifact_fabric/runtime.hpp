#pragma once
// Artifact Fabric - top-level runtime facade.
#include "artifact_fabric/catalog.hpp"
namespace af {
class Runtime {
 public:
  Catalog& catalog() { return cat_; }
 private:
  Catalog cat_;
};
}  // namespace af
