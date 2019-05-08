/*===------------- math.h - Alternative math.h header ----------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __cplusplus
#include <__clang_openmp_math.h>
#endif

#ifndef __CLANG_NO_HOST_MATH__
#include_next <math.h>
#else
#undef __CLANG_NO_HOST_MATH__
#endif

