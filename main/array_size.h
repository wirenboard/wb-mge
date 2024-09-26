/**
 * Rusty Russell’s patch to Linux’s ARRAY_SIZE macro
 * http://zubplot.blogspot.com/2015/01/gcc-is-wonderful-better-arraysize-macro.html
 *
 * This is fail to compile if trying to get the size of a pointer instead of an array.
 * How it works:
 *     int a[100];          // typeof(a) = int[]    typeof(&a[0]) = int*
 *     int *p = a;          // typeof(p) = int*     typeof(&p[0]) = int*
 *
 * If case of ARRAY_SIZE(p) types are compatible, __builtin_types_compatible_p returns 1
 * and sizeof(typeof(int[-1])) is fail to compile.
 */
#define ARRAY_SIZE(arr)                 (sizeof(arr) / sizeof((arr)[0]) + sizeof(typeof(int[1 - 2 * !!__builtin_types_compatible_p(typeof(arr), typeof(&arr[0]))])) * 0)

