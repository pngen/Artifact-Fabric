// Example 10: single-flight production - one producer, shared result.
#include "ex_fw.hpp"
#include <atomic>
#include <thread>
#include <vector>
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  ArtifactId target = ArtifactId::random();
  std::atomic<int> builds{0};
  std::vector<ArtifactId> out(8);
  std::vector<std::thread> ts;
  for (int i=0;i<8;++i) ts.emplace_back([&,i]{ out[i]=cat.get_or_build(target,[&]{ ++builds; PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={1,2,3,4}; req.authority=auth_for(cat,ProducerId::random()); return cat.publish(req); }, auth_for(cat,ProducerId::random())); });
  for (auto& t: ts) t.join();
  std::printf("builds=%d all_same=%s\n", builds.load(), (out[0]==out[7])?"true":"false");
  return 0;
}
