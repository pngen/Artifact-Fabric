// Example 9: identical content shares one physical backing.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  std::vector<std::uint8_t> content = {1,2,3,4,5,6,7,8};
  for (int i=0;i<3;++i){ PublishRequest req; req.descriptor=kernel_desc(1,8); req.content=content; req.authority=auth_for(cat,ProducerId::random()); cat.publish(req); }
  Accounting a = cat.accounting();
  std::printf("logical artifacts=%d physical backing=%d refcount=%d logical_bytes=%lld dedup_bytes=%lld\n",
    (int)a.logical_artifacts, (int)a.physical_backing, (int)cat.content_reference_count(ContentDigest(Sha256::digest("12345678", 8))),
    (long long)a.logical_bytes, (long long)a.dedup_bytes);
  return 0;
}
