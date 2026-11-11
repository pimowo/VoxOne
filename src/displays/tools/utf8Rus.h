#ifndef utf8Rus_h
#define  utf8Rus_h

#include <stddef.h>

size_t strlen_utf8(const char* s);
size_t utf8Rus(char* output, size_t outputSize, const char* str);

#endif
