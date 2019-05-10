#include <string>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <list>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "../../ELF/PLOBBReordering.h"
#include "../../ELF/PLOELFCfg.h"

using llvm::StringRef;

using std::list;
using std::string;
using namespace lld;
using namespace llvm;
namespace opts {

static cl::opt<std::string>
CfgRead(
  "cfg-read",
  cl::desc("File to read the Cfg from."),
  cl::Required);

static cl::opt<std::string>
CfgDump(
  "cfg-dump",
  cl::desc("File to dump the cfg to."),
  cl::ZeroOrMore);

static cl::opt<std::string>
LayoutDump(
  "layout-dump",
  cl::desc("File to dump the layout to."),
  cl::ZeroOrMore);
}

int main(int Argc, const char **Argv) {
  cl::ParseCommandLineOptions(Argc, Argv, "Layout");
  StringRef CfgFile = opts::CfgRead.getValue();
  auto CfgReader = lld::plo::ELFCfgReader(CfgFile);
  CfgReader.readCfgs();
  fprintf(stderr, "Read all Cfgs\n");

  if (!opts::CfgDump.empty()){
    std::ofstream OS;
    OS.open(opts::CfgDump.getValue(), std::ios::out);

    if (!OS.good()) {
      fprintf(stderr, "File is not good for writing: <%s>\n", opts::CfgDump.getValue().c_str());
      exit(0);
    } else {
        for (auto &Cfg: CfgReader.Cfgs) {
          Cfg->dumpToOS(OS);
        }
      OS.close();
    }
  }

  if(!opts::LayoutDump.empty()){
    list<StringRef> SymbolOrder;
    for(auto& Cfg: CfgReader.Cfgs){
      if (Cfg->isHot()){
        auto ChainBuilder = lld::plo::ExtTSPChainBuilder(Cfg.get());
        ChainBuilder.doSplitOrder(SymbolOrder, SymbolOrder.end(), SymbolOrder.end());
      } else {
        for(auto& Node: Cfg->Nodes)
          SymbolOrder.push_back(Node->ShName);
      }
    }

    std::ofstream LOS;
    LOS.open(opts::LayoutDump.getValue(), std::ios::out);
    for(auto& Symbol: SymbolOrder)
      LOS << Symbol.str() << "\n";
    LOS.close();
  }
}
