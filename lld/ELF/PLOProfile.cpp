#include "PLOProfile.h"

#include "PLO.h"
#include "PLOELFCfg.h"

#include <iostream>
#include <ostream>
using std::endl;
using std::ostream;

namespace lld {
namespace plo {

LBREntry *LBREntry::CreateEntry(const StringRef &SR) {
  unique_ptr<LBREntry> EP(new LBREntry());
  auto L1 = SR.split('/');
  // Passing "0" as radix enables autosensing, so no need to skip "0x"
  // prefix.
  if (L1.first.empty() || L1.first.getAsInteger(0, EP->From))
    return nullptr;
  auto L2 = L1.second.split('/');
  if (L2.first.empty() || L2.first.getAsInteger(0, EP->To))
    return nullptr;
  auto L3 = L2.second.split('/');
  if (L3.first.empty() || L3.second.empty())
    return nullptr;
  EP->Predict = *(L3.first.data());
  if (EP->Predict != 'M' && EP->Predict != 'P' && EP->Predict != '-')
    return nullptr;
  if (L3.second.rsplit('/').second.getAsInteger(10, EP->Cycles))
    return nullptr;
  return EP.release();
}

void PLOProfile::ProcessLBRs() {
  int Total = 0;
  uint32_t Strange = 0, Strange2 = 0;
  uint32_t IntraFunc = 0, InterFunc = 0;
  uint64_t LastFromAddr{0}, LastToAddr{0};
  uint32_t Idx = 0;
  for (auto  &Record : LBRs) {
    ++Idx;
    Total += Record->Entries.size();
    LBREntry *LastEntry{nullptr};
    ELFCfg *LastToCfg{nullptr};
    ELFCfgNode *LastToNode{nullptr};

    // The fist entry in the record is the branch that happens last in
    // history.  The second entry happens earlier than the first one,
    // ..., etc.  So we iterate the entries in reverse order - the
    // earliest in history -> the latest.
    for (auto P = Record->Entries.rbegin(), Q = Record->Entries.rend();
         P != Q; ++P) {
      auto &Entry = *P;
      uint64_t From = Entry->From, To = Entry->To;
      ELFCfg *FromCfg, *ToCfg;
      ELFCfgNode *FromNode, *ToNode;
      Plo.FindCfgForAddress(From, FromCfg, FromNode);
      Plo.FindCfgForAddress(To, ToCfg, ToNode);

      if (FromCfg && FromCfg == ToCfg) {
        FromCfg->MapBranch(FromNode, ToNode);
        ++IntraFunc;
      } else if (FromCfg && ToCfg /* implies: FromCfg != ToCfg */ ) {
        FromCfg->MapCallOut(FromNode, ToNode);
        ++InterFunc;
      }
      // Mark everything between LastToCfg[LastToNode] and FromCfg[FromNode].
      if (LastToCfg == FromCfg) {
        if (LastToCfg->MarkPath(LastToNode, FromNode) == false) {
          if (!(LastFromAddr == From && LastToAddr == To
		&& std::next(P) == Q)) {
            ++Strange;
            std::cout << "*****" << endl;
            std::cout << "Failed to map: "
                 << *LastToNode << " -> "
                 << *FromNode << endl;
            std::cout << *FromCfg << endl;
            
            // fprintf(stderr, "*****\n");
            // fprintf(stderr, "Failed to map %s -> %s\n",
            //         LastToNode->ShName.str().c_str(),
            //         FromNode->ShName.str().c_str());
            // PrintLBRRecord(Record.get());
            // LastToCfg->Diagnose();
            // fprintf(stderr, "*****\n");
          }
        }
      } else {
        if (LastToCfg && FromCfg && LastToCfg != FromCfg) {
          if (!(LastFromAddr == From && LastToAddr == To
		&& std::next(P) == Q)) {
            std::cout << "Strange2: ===== " << std::dec << Idx << endl;
            std::cout << "Last entry: " << *LastEntry << endl;
            std::cout << "Entry: " << *Entry << endl;
            std::cout << "Last: " << *LastToNode << endl;
            std::cout << "From: " << *FromNode << endl;
            ++Strange2;
          }
        }
      }
      LastEntry = Entry.get();
      LastToCfg = ToCfg;
      LastToNode = ToNode;
      LastFromAddr = From;
      LastToAddr = To;
    }
  }

  fprintf(stderr, "Total strange: %d\n", Strange);
  fprintf(stderr, "Total strange2: %d\n", Strange2);
  fprintf(stderr, "Total Intra: %d\n", IntraFunc);
  fprintf(stderr, "Total Inter: %d\n", InterFunc);
}

ostream & operator << (ostream &Out, const LBREntry &Entry) {
  ELFCfg *FromCfg, *ToCfg;
  ELFCfgNode *FromNode, *ToNode;
  Plo.FindCfgForAddress(Entry.From, FromCfg, FromNode);
  Plo.FindCfgForAddress(Entry.To, ToCfg, ToNode);
  Out << (FromNode ? FromNode->ShName.str().c_str() : "NA")
      << "(" << std::showbase << std::hex << Entry.From << ") -> "
      << (ToNode ? ToNode->ShName.str().c_str() : "NA")
      << "(" << std::showbase << std::hex << Entry.To << ")";
  return Out;
}

ostream & operator << (ostream &Out, const LBRRecord &R) {
  Out << "==== LBR Record ====" << endl;
  for (auto P = R.Entries.rbegin(), Q = R.Entries.rend(); P != Q; ++P)
    Out << **P << endl;
  Out << "==== End of LBR Record ====" << endl;
  return Out;
}

}  // end of namespace plo
}  // end of namespace lld
