// Example 12: distributed mutation authority and epoch fencing.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={2,2,2,2}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  cat.roll_epoch(WorkerBootId::random());  // stale authority from epoch 1
  AuthorityEnvelope stale = auth_for(cat, ProducerId::random()); stale.epoch = 1;
  bool rejected=false;
  try { cat.publish({kernel_desc(2,4), {3,3,3,3}, stale}); } catch (...) { rejected=true; }
  std::printf("stale-epoch publish rejected=%s\n", rejected?"true":"false");
  return 0;
}
