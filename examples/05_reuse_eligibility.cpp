// Example 5: first-class reuse eligibility.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={5,5,5,5}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  CompatRequirement rq; rq.require_reusable=true;
  ReuseResult r = cat.reuse(p.id, rq);
  std::printf("reuse=%s\n", r.reusable?"REUSABLE":"NOT_REUSABLE");
  cat.invalidate(p.id, "model revision changed", auth_for(cat, ProducerId::random()));
  ReuseResult r2 = cat.reuse(p.id, rq);
  std::printf("after invalidation reuse=%s (%s)\n", r2.reusable?"REUSABLE":"NOT_REUSABLE", r2.reason_text().c_str());
  return 0;
}
