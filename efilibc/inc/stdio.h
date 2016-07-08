/* Copyright (C) 2014 by John Cronin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdarg.h>

#ifndef STDIO_H
#define STDIO_H

#ifndef IN_FILE_C
typedef void FILE;
#endif
#include <efi.h>
#include <efilib.h>
typedef struct _FILE
{
        EFI_FILE *f;
        int eof;
        int error;
        int fileno;
        int istty;
        int ttyno;
} _FILE;

#define stdin	((FILE *)0)
#define stdout	((FILE *)1)
#define stderr	((FILE *)2)

#define EOF		0xff

int printf(const char *fmt, ...);
int sprintf(char *buffer, const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);

int scanf(const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vsscanf(const char *str, const char *format, va_list ap);

int puts(const char *s);
int putchar(int c);
int fputc(int c, FILE *stream);
#define putc(c, stream) (fputc(c, stream))

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
#define getc(s) (fgetc(s))
#define getchar() (fgetc(stdin))
char *gets(char *s);

FILE *fopen(const char *path, const char *mode);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fclose(FILE *fp);
int fflush(FILE *stream);

int remove(const char *pathname);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
int fileno(FILE *stream);

#define SEEK_SET		1
#define SEEK_CUR		2
#define SEEK_END		3

typedef long fpos_t;
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, fpos_t *pos);

// non standard crap
int f_is_dir(FILE *stream);

#ifndef POSIXLY_CORRECT
long fsize(FILE *stream);
#endif

#endif
