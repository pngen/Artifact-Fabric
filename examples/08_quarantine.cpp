// Example 8: corrupt artifacts are quarantined, never silently reused.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={1,2,3,4}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  QuarantineRecord qr; qr.reason=QuarantineReason::INTEGRITY_FAILURE; qr.detection_source="checksum"; qr.timestamp_ns=1000;
  cat.quarantine(p.id, qr, auth_for(cat, ProducerId::random()));
  CompatRequirement rq; rq.require_reusable=true;
  std::printf("quarantined lifecycle=%s reuse=%s\n", lifecycle_name(cat.find(p.id)->lifecycle), cat.reuse(p.id, rq).reusable?"REUSABLE":"NOT_REUSABLE");
  return 0;
}
