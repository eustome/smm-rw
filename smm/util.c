#include "stdafx.h"

int _fltused = 0;
void *memcpy(void *d, const void *s, unsigned long long n) { char *a = d; const char *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned long long n) { char *a = d; while (n--) *a++ = (char)c; return d; }
