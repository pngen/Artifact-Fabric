// Example 4: deterministic typed compatibility check, exact failed dims.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={3,3,3,3}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  CompatRequirement good; good.compute_capability="12.0"; good.abi="sm_120"; good.require_reusable=true;
  CompatRequirement bad; bad.abi="sm_90"; bad.require_reusable=true;
  CompatResult r1 = evaluate_compatibility(*cat.find(p.id), good);
  CompatResult r2 = evaluate_compatibility(*cat.find(p.id), bad);
  std::printf("good=%s bad=%s (%s) dims=", r1.compatible()?"COMPATIBLE":"INCOMPATIBLE", r2.compatible()?"COMPATIBLE":"INCOMPATIBLE", compat_outcome_name(r2.outcome));
  for (auto& f : r2.failed_dimensions) std::printf("%s ", f.c_str());
  std::printf("\n");
  return 0;
}
