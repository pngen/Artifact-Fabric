// Example 14: generations roll independently and stay distinguishable.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  ArtifactGeneration g1(1);
  ArtifactGeneration g2(2);
  ToolchainGeneration tc1(1);
  std::printf("artifact gen1=%s gen2=%s toolchain gen1=%s\n", g1.to_string().c_str(), g2.to_string().c_str(), tc1.to_string().c_str());
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest a; a.descriptor=kernel_desc(1,4); a.content={1,1,1,1}; a.authority=auth_for(cat,ProducerId::random());
  PublishResult p1 = cat.publish(a);
  PublishRequest b; b.descriptor=kernel_desc(2,4); b.content={2,2,2,2}; b.authority=auth_for(cat,ProducerId::random());
  PublishResult p2 = cat.publish(b);
  std::printf("gen1=%llu gen2=%llu distinct=%s\n", (unsigned long long)cat.find(p1.id)->generation.value(),
    (unsigned long long)cat.find(p2.id)->generation.value(), (cat.find(p1.id)->generation!=cat.find(p2.id)->generation)?"true":"false");
  return 0;
}
