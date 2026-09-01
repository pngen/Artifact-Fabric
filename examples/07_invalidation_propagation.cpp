// Example 7: invalidation propagates to dependents.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest dep; dep.descriptor=kernel_desc(1,4); dep.content={8,8,8,8}; dep.authority=auth_for(cat,ProducerId::random());
  PublishResult d = cat.publish(dep);
  ArtifactDescriptor prod = kernel_desc(1,4); prod.kind=ArtifactKind::ENGINE_ARTIFACT;
  DependencyRef dr; dr.artifact=d.id; dr.generation=DependencyGeneration(1); dr.digest=d.content_digest; prod.dependencies.push_back(dr);
  PublishRequest preq; preq.descriptor=prod; preq.content={9,9,9,9}; preq.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(preq);
  cat.invalidate(d.id, "dependency corruption", auth_for(cat, ProducerId::random()));
  std::printf("dependency=%s product=%s\n", lifecycle_name(cat.find(d.id)->lifecycle), lifecycle_name(cat.find(p.id)->lifecycle));
  return 0;
}
