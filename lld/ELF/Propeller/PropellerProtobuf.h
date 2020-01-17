#ifndef LLD_ELF_PROPELLER_PROTOBUF_H
#define LLD_ELF_PROPELLER_PROTOBUF_H
#ifdef PROPELLER_PROTOBUF

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "lld/Common/ErrorHandler.h"
#include "propeller_cfg.pb.h"
#include "llvm/Support/raw_ostream.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <list>
#include <string>

namespace lld {
namespace propeller {

class ControlFlowGraph;
class CFGNode;

class ProtobufPrinter {
public:
  static ProtobufPrinter *create(const std::string &name) {
    int fd = open(name.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    if (fd == -1) {
      error(StringRef("Failed to create/open '") + name + "'.");
      return nullptr;
    }
    return new ProtobufPrinter(name, fd);
  }

  void addCFG(ControlFlowGraph &cfg,
              std::list<CFGNode *> *orderedBBs = nullptr);

  void clearCFGGroup() { cfgGroupPb.clear_cfg_list(); }

  void printCFGGroup();

  ~ProtobufPrinter() { outStream.Close(); }

private:
  ProtobufPrinter(const std::string &name, int fd)
      : outName(name), outStream(fd) {}

  void populateCFGPB(llvm::plo::cfg::CFG &cfgpb, const ControlFlowGraph &cfg);

  std::string outName;
  google::protobuf::io::FileOutputStream outStream;
  llvm::plo::cfg::CFGGroup cfgGroupPb;
};

} // namespace propeller
} // namespace lld

#endif
#endif
