// Example 1: publish a basic artifact and inspect its descriptor.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  ProducerId p = ProducerId::random();
  ArtifactDescriptor d = kernel_desc(1, 4);
  PublishRequest req; req.descriptor = d; req.content = {1,2,3,4}; req.authority = auth_for(cat, p);
  PublishResult pr = cat.publish(req);
  const ArtifactDescriptor* g = cat.find(pr.id);
  std::printf("published %s kind=%s lifecycle=%s content=%s\n", pr.id.to_string().c_str(),
              kind_name(g->kind), lifecycle_name(g->lifecycle), g->content_digest.to_string().c_str());
  return 0;
}
