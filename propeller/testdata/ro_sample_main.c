// To build ro_sample.out:
// clang -fuse-ld=lld -Wl,-build-id ro_sample_main.c libro_sample.so
// -Wl,-build-id -Wl,-rpath,. -o ro_sample.out

int foo();

int main() { return foo(); }
