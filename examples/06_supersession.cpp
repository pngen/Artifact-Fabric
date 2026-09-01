// Example 6: supersede the old artifact without rewriting history.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest r1; r1.descriptor=kernel_desc(1,4); r1.content={6,6,6,6}; r1.authority=auth_for(cat,ProducerId::random());
  PublishResult old_ = cat.publish(r1);
  PublishRequest r2; r2.descriptor=kernel_desc(2,4); r2.content={7,7,7,7}; r2.authority=auth_for(cat,ProducerId::random());
  PublishResult neu = cat.publish(r2);
  cat.supersede(old_.id, neu.id, "newer ABI", auth_for(cat, ProducerId::random()));
  std::printf("old lifecycle=%s, new lifecycle=%s\n", lifecycle_name(cat.find(old_.id)->lifecycle), lifecycle_name(cat.find(neu.id)->lifecycle));
  auto pred = cat.explain(neu.id).superseded_predecessor;
  std::printf("new has %d predecessor(s)\n", (int)pred.size());
  return 0;
}
