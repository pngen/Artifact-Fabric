// Example 3: explicit dependency graph + cycle rejection.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest dep; dep.descriptor=kernel_desc(1,4); dep.content={1,1,1,1}; dep.authority=auth_for(cat,ProducerId::random());
  PublishResult d = cat.publish(dep);
  ArtifactDescriptor prod = kernel_desc(1,4); prod.kind = ArtifactKind::ENGINE_ARTIFACT;
  DependencyRef dr; dr.artifact = d.id; dr.generation = DependencyGeneration(1); dr.digest = d.content_digest;
  prod.dependencies.push_back(dr);
  PublishRequest preq; preq.descriptor=prod; preq.content={2,2,2,2}; preq.authority=auth_for(cat,ProducerId::random());
  PublishResult pr = cat.publish(preq);
  auto deps = cat.dependencies_of(pr.id); auto depusers = cat.dependents_of(d.id);
  std::printf("product depends on %d, dependency has %d users\n", (int)deps.size(), (int)depusers.size());
  bool cycle = false;
  DependencyGraph g; ArtifactId x=ArtifactId::random(), y=ArtifactId::random(); g.add_node(x); g.add_node(y);
  DependencyRef e1; e1.artifact=y; try { g.add_edge(x,e1); } catch(...) {}
  DependencyRef e2; e2.artifact=x; try { g.add_edge(y,e2); } catch(...) { cycle=true; }
  std::printf("cycle rejected: %s\n", cycle?"true":"false");
  return 0;
}
