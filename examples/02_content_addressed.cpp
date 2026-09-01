// Example 2: identical content is content-addressed to the same digest.
#include "ex_fw.hpp"
using namespace afdemo;
int main() {
  Catalog cat; cat.set_authority(1, boot);
  PublishRequest a; a.descriptor = kernel_desc(1,4); a.content={7,8,9,10}; a.authority=auth_for(cat,ProducerId::random());
  PublishRequest b; b.descriptor = kernel_desc(1,4); b.content={7,8,9,10}; b.authority=auth_for(cat,ProducerId::random());
  PublishResult pa = cat.publish(a); PublishResult pb = cat.publish(b);
  std::printf("same content => same digest: %s\n", (pa.content_digest==pb.content_digest)?"true":"false");
  std::printf("content-addressed find by digest count = %d\n", (int)cat.find_by_content(pa.content_digest).size());
  return 0;
}
