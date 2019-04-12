#include "PLOProfile.h"

#include "PLO.h"
#include "PLOELFCfg.h"

#include <fstream>
#include <iostream>

#include <llvm/ADT/SmallVector.h>

using std::ostream;
using std::string;

using llvm::SmallVector;

namespace lld {
namespace plo {

bool LBREntry::FillEntry(const StringRef &SR, LBREntry &Entry) {
  auto L1 = SR.split('/');
  // Passing "0" as radix enables autosensing, so no need to skip "0x"
  // prefix.
  if (L1.first.empty() || L1.first.getAsInteger(0, Entry.From))
    return false;
  auto L2 = L1.second.split('/');
  if (L2.first.empty() || L2.first.getAsInteger(0, Entry.To))
    return false;
  auto L3 = L2.second.split('/');
  if (L3.first.empty() || L3.second.empty())
    return false;
  Entry.Predict = *(L3.first.data());
  if (Entry.Predict != 'M' && Entry.Predict != 'P' && Entry.Predict != '-')
    return false;
  if (L3.second.rsplit('/').second.getAsInteger(10, Entry.Cycles))
    return false;
  return true;
}

bool PLOProfile::ProcessProfile() {
  std::ifstream fin(ProfileName.str());
  if (!fin.good()) return false;
  string line;
  // Preallocate "Entries" (total size = sizeof(LBREntry) * 32 bytes =
  // 6k). Which is way more faster than create space everytime.
  LBREntry EntryArray[32];
  int EntryIndex;
  while (fin.good() && !std::getline(fin, line).eof()) {
    if (line.empty()) continue;
    EntryIndex = 0;
    const char *p = line.c_str();
    const char *q = p + 1;
    do {
      while (*(q++) != ' ');
      StringRef EntryString = StringRef(p, q - p - 1);
      if (!LBREntry::FillEntry(EntryString, EntryArray[EntryIndex])) {
	fprintf(stderr, "Invalid entry: %s\n", EntryString.str().c_str());
	break;
      }
      if (*q == '\0') break;
      p = q + 1;
      q = p + 1;
      ++EntryIndex;
    } while(true);
    if (EntryIndex) {
      ProcessLBR(EntryArray, EntryIndex);
    }
  }
  fprintf(stderr, "Intra-func marked: %lu (%lu not marked)\n",
          IntraFunc, NonMarkedIntraFunc);
  fprintf(stderr, "Inter-func marked: %lu (%lu not marked)\n",
          InterFunc, NonMarkedInterFunc);
  return true;
}

void PLOProfile::ProcessLBR(LBREntry *EntryArray, int EntryIndex) {
  ELFCfg *LastToCfg{nullptr};
  ELFCfgNode *LastToNode{nullptr};
  uint64_t LastFromAddr{0}, LastToAddr{0};

  // The fist entry in the record is the branch that happens last in
  // history.  The second entry happens earlier than the first one,
  // ..., etc.  So we iterate the entries in reverse order - the
  // earliest in history -> the latest.
  for (int P = EntryIndex - 1; P >= 0; --P) {
    auto &Entry = EntryArray[P];
    uint64_t From = Entry.From, To = Entry.To;
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
      ++IntraFunc;
      if (LastToCfg->MarkPath(LastToNode, FromNode) == false) {
        if (!(LastFromAddr == From && LastToAddr == To && P == 0)) {
          ++NonMarkedIntraFunc;
          // std::cout << "*****" << std::endl;
          // std::cout << "Failed to map " << std::showbase << std::hex
          //           << LastToAddr << " -> " << From
          //           << " LBR@" << std::noshowbase << std::dec << Idx << " : "
          //           << *LastToNode << " -> "
          //           << *FromNode << std::endl;
          // std::cout << *FromCfg << std::endl;
        }
      }
    } else {
      ++InterFunc;
      if (LastToCfg && FromCfg && LastToCfg != FromCfg) {
        if (!(LastFromAddr == From && LastToAddr == To && P == 0)) {
          // std::cout << "Failed to map: " << std::showbase << std::hex
          //           << LastToAddr << " -> " << From << std::endl;
          // std::cout << "Last entry:    " << *LastEntry << std::endl;
          // std::cout << "Current Entry: " << *Entry << std::endl;
          // std::cout << "Last: " << *LastToNode << std::endl;
          // std::cout << "From: " << *FromNode << std::endl;
          // exit(1);
          ++NonMarkedInterFunc;
        }
      }
    }
    LastToCfg = ToCfg;
    LastToNode = ToNode;
    LastFromAddr = From;
    LastToAddr = To;
  }
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
  Out << "==== LBR Record ====" << std::endl;
  for (auto P = R.Entries.rbegin(), Q = R.Entries.rend(); P != Q; ++P)
    Out << **P << std::endl;
  Out << "==== End of LBR Record ====" << std::endl;
  return Out;
}

}  // end of namespace plo
}  // end of namespace lld
