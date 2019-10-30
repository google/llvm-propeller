#ifndef LLD_ELF_PROPELLER_PROTOBUF_H
#define LLD_ELF_PROPELLER_PROTOBUF_H
#ifdef PROPELLER_PROTOBUF

namespace lld {
namespace elf {
namespace propeller {

class ProtobufPrinter {
public:
  ProtobufPrinter *create(StringRef outputName);
};

  
}
}
}


#endif
#endif

