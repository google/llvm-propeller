#include <iostream>
#include <regex>
#include <string>

using namespace std;

int main(const int argc, const char *argv[]) {
  bool exp = false;
  for (int i = 1; i < argc; ++i)
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--exp") || !strcmp(argv[i], "-exp"))
      exp = true;
  string line;
  regex R("\\b([alLr]+)(\\.BB\\.[a-zA-Z0-9_$-]+)\\b");
  while (getline(cin, line)) {
    smatch result;
    auto begin = line.cbegin(), end = line.cend();
    while(regex_search(begin, end, result, R)) {
      const ssub_match &sm = result[1];
      string smstr = sm.str();
      cout << result.prefix() << smstr.size() << result[2].str();
      if (exp && smstr[0] != 'a') cout << '(' << smstr[0] << ')';
      begin += result.prefix().str().size() + result.str().size();
    }
    cout << string(begin, end) << endl;
  }
  return 0;
}
