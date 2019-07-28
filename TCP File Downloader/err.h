#ifndef _ERR_
#define _ERR_

/* Wypisuje informację o błędnym zakończeniu funkcji systemowej
i kończy działanie programu. */
extern void syserr(const char *fmt, ...) __attribute__((noreturn));

/* Wypisuje informację o błędzie i kończy działanie programu. */
extern void fatal(const char *fmt, ...) __attribute__((noreturn));

#endif
