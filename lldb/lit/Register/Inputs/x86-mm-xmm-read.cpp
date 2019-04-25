#include <cstdint>

struct alignas(16) xmm_t {
  uint64_t a, b;
};

int main() {
  constexpr uint64_t mm0 = 0x0102030405060708;
  constexpr uint64_t mm1 = 0x1112131415161718;
  constexpr uint64_t mm2 = 0x2122232425262728;
  constexpr uint64_t mm3 = 0x3132333435363738;
  constexpr uint64_t mm4 = 0x4142434445464748;
  constexpr uint64_t mm5 = 0x5152535455565758;
  constexpr uint64_t mm6 = 0x6162636465666768;
  constexpr uint64_t mm7 = 0x7172737475767778;

  constexpr xmm_t xmm0 = { 0x020406080A0C0E01, 0x030507090B0D0F00 };
  constexpr xmm_t xmm1 = { 0x121416181A1C1E11, 0x131517191B1D1F10 };
  constexpr xmm_t xmm2 = { 0x222426282A2C2E21, 0x232527292B2D2F20 };
  constexpr xmm_t xmm3 = { 0x323436383A3C3E31, 0x333537393B3D3F30 };
  constexpr xmm_t xmm4 = { 0x424446484A4C4E41, 0x434547494B4D4F40 };
  constexpr xmm_t xmm5 = { 0x525456585A5C5E51, 0x535557595B5D5F50 };
  constexpr xmm_t xmm6 = { 0x626466686A6C6E61, 0x636567696B6D6F60 };
  constexpr xmm_t xmm7 = { 0x727476787A7C7E71, 0x737577797B7D7F70 };

  asm volatile(
    "movq    %0, %%mm0\n\t"
    "movq    %1, %%mm1\n\t"
    "movq    %2, %%mm2\n\t"
    "movq    %3, %%mm3\n\t"
    "movq    %4, %%mm4\n\t"
    "movq    %5, %%mm5\n\t"
    "movq    %6, %%mm6\n\t"
    "movq    %7, %%mm7\n\t"
    "\n\t"
    "movaps  %8, %%xmm0\n\t"
    "movaps  %9, %%xmm1\n\t"
    "movaps  %10, %%xmm2\n\t"
    "movaps  %11, %%xmm3\n\t"
    "movaps  %12, %%xmm4\n\t"
    "movaps  %13, %%xmm5\n\t"
    "movaps  %14, %%xmm6\n\t"
    "movaps  %15, %%xmm7\n\t"
    "\n\t"
    "int3"
    :
    : "rm"(mm0), "rm"(mm1), "rm"(mm2), "rm"(mm3), "rm"(mm4), "rm"(mm5),
      "rm"(mm6), "rm"(mm7), "m"(xmm0), "m"(xmm1), "m"(xmm2), "m"(xmm3),
      "m"(xmm4), "m"(xmm5), "m"(xmm6), "m"(xmm7)
    : "%mm0", "%mm1", "%mm2", "%mm3", "%mm4", "%mm5", "%mm6", "%mm7",
      "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"
  );

  return 0;
}
