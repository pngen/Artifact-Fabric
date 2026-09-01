// Example 15: deterministic explanation (text, JSON, stable digest).
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest req; req.descriptor=kernel_desc(1,4); req.content={9,8,7,6}; req.authority=auth_for(cat,ProducerId::random());
  PublishResult p = cat.publish(req);
  Explanation ex = cat.explain(p.id);
  std::printf("%s", ex.to_text().c_str());
  std::printf("json: %s\n", ex.to_json().c_str());
  std::printf("explain_digest: %s\n", ex.digest().c_str());
  return 0;
}
