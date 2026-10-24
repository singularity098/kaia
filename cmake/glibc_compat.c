/* Local versions of glibc functions whose newest symbol versions are newer than glibc 2.35.
   They are hidden, so code in the plugin (including the statically linked libstdc++) binds to
   them instead of to the system library, and the binary loads on glibc 2.35.
   Compiled as plain C11 without LTO so the .symver directives below are honoured. */
#if defined(__linux__) && defined(__x86_64__)
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <wchar.h>
#include <sys/random.h>

#define HIDDEN __attribute__((visibility("hidden")))

HIDDEN int kaia_glibc_compat_anchor = 1;

/* math: bind to the original 2.2.5 versions */
double old_fmod (double, double);
float old_fmodf (float, float);
double old_hypot (double, double);
float old_hypotf (float, float);
__asm__(".symver old_fmod,fmod@GLIBC_2.2.5");
__asm__(".symver old_fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver old_hypot,hypot@GLIBC_2.2.5");
__asm__(".symver old_hypotf,hypotf@GLIBC_2.2.5");
HIDDEN double fmod (double x, double y)   { return old_fmod (x, y); }
HIDDEN float fmodf (float x, float y)     { return old_fmodf (x, y); }
HIDDEN double hypot (double x, double y)  { return old_hypot (x, y); }
HIDDEN float hypotf (float x, float y)    { return old_hypotf (x, y); }

/* glibc 2.38 C23 parsing functions (referenced by libstdc++ built against new glibc) */
HIDDEN long __isoc23_strtol (const char* s, char** e, int b)                 { return strtol (s, e, b); }
HIDDEN long long __isoc23_strtoll (const char* s, char** e, int b)           { return strtoll (s, e, b); }
HIDDEN unsigned long __isoc23_strtoul (const char* s, char** e, int b)       { return strtoul (s, e, b); }
HIDDEN unsigned long long __isoc23_strtoull (const char* s, char** e, int b) { return strtoull (s, e, b); }
HIDDEN long __isoc23_wcstol (const wchar_t* s, wchar_t** e, int b)           { return wcstol (s, e, b); }
HIDDEN unsigned long __isoc23_wcstoul (const wchar_t* s, wchar_t** e, int b) { return wcstoul (s, e, b); }
HIDDEN long long __isoc23_wcstoll (const wchar_t* s, wchar_t** e, int b)     { return wcstoll (s, e, b); }
HIDDEN unsigned long long __isoc23_wcstoull (const wchar_t* s, wchar_t** e, int b) { return wcstoull (s, e, b); }
HIDDEN int __isoc23_vsscanf (const char* s, const char* f, va_list a)        { return vsscanf (s, f, a); }
HIDDEN int __isoc23_sscanf (const char* s, const char* f, ...)
{
    va_list a; va_start (a, f); int r = vsscanf (s, f, a); va_end (a); return r;
}
HIDDEN int __isoc23_vswscanf (const wchar_t* s, const wchar_t* f, va_list a) { return vswscanf (s, f, a); }
HIDDEN int __isoc23_swscanf (const wchar_t* s, const wchar_t* f, ...)
{
    va_list a; va_start (a, f); int r = vswscanf (s, f, a); va_end (a); return r;
}

/* glibc 2.36 arc4random (used by std::random_device in GCC 13's libstdc++) */
HIDDEN void arc4random_buf (void* buf, size_t n)
{
    unsigned char* p = (unsigned char*) buf;
    while (n > 0)
    {
        ssize_t got = getrandom (p, n, 0);
        if (got <= 0)
        {
            FILE* f = fopen ("/dev/urandom", "rb");
            if (f) { got = (ssize_t) fread (p, 1, n, f); fclose (f); }
            if (got <= 0) abort();
        }
        p += got;
        n -= (size_t) got;
    }
}
HIDDEN uint32_t arc4random (void)
{
    uint32_t v;
    arc4random_buf (&v, sizeof v);
    return v;
}
HIDDEN uint32_t arc4random_uniform (uint32_t bound)
{
    if (bound < 2) return 0;
    uint32_t min = (uint32_t) -bound % bound, r;
    do { r = arc4random(); } while (r < min);
    return r % bound;
}
#endif
