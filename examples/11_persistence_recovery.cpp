// Example 11: durable persistence + recovery reproduces stable state.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(3, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={4,4,4,4}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  auto img = cat.save();
  Catalog cat2; cat2.set_authority(3, boot); cat2.load(img);
  const ArtifactDescriptor* g2 = cat2.find(p.id);
  std::printf("recovered %s digest=%s size=%lld\n", g2?g2->id.to_string().c_str():"<none>",
    g2?g2->content_digest.to_string().c_str():"<none>", g2?(long long)g2->size_bytes:0);
  std::printf("accounting_zero=%s\n", cat2.accounting_is_zero()?"true":"false");
  return 0;
}
