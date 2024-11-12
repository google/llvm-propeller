// This file is used to generate propeller_unique_names.out which
// contains duplicate uniq-named functions. See BUILD rule
// ":duplicate_unique_names".

static int foo() { return 20; }

#ifdef VER1
int goo() { return foo(); }
#elif defined VER2
int goo2() { return foo() + 5; }
#endif

#ifdef MAIN
int goo();
int goo2();
int main(int argc, char* argv[]) {
  return goo() + goo2() + reinterpret_cast<unsigned long>(foo);  // NOLINT
}
#endif
