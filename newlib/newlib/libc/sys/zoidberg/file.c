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

#include <efi.h>
#include <efilib.h>

typedef struct ZOIDBERG_FILE
{
       EFI_FILE *f;
       int eof;
       int error;
       int fileno;
       int istty;
       int ttyno;
} ZOIDBERG_FILE;

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <efilibc.h>

#define zoidberg_stdin   ((ZOIDBERG_FILE *)0)
#define zoidberg_stdout  ((ZOIDBERG_FILE *)1)
#define zoidberg_stderr  ((ZOIDBERG_FILE *)2)

/* fileno -> FILE * mappings */
#define MAX_FILENO		1024
static ZOIDBERG_FILE* fileno_map[MAX_FILENO] = { zoidberg_stdin, zoidberg_stdout, zoidberg_stderr };
static int next_fileno = 3;

size_t console_fread(void *ptr, size_t size, size_t nmemb, void *data);
size_t console_fwrite(const void *ptr, size_t size, size_t nmemb, void *data);

EFI_FILE *fopen_root = NULL;

static fwrite_func stdout_fwrite = NULL;
static fwrite_func stderr_fwrite = NULL;
static fread_func stdin_fread = NULL;
static void *stdout_data = NULL;
static void *stderr_data = NULL;
static void *stdin_data = NULL;

int efilibc_register_stdout_fwrite(fwrite_func new_stdout_fwrite, void *data)
{
	stdout_fwrite = new_stdout_fwrite;
	stdout_data = data;
	return 0;
}

int efilibc_register_stderr_fwrite(fwrite_func new_stderr_fwrite, void *data)
{
	stderr_fwrite = new_stderr_fwrite;
	stderr_data = data;
	return 0;
}

int efilibc_register_stdin_fread(fread_func new_stdin_fread, void *data)
{
	stdin_fread = new_stdin_fread;
	stdin_data = data;
	return 0;
}

void conv_backslashes(CHAR16 *s)
{
	while (*s != 0)
	{
		if(*s == '/')
			*s = '\\';
		s++;
	}
}

int zoidberg_mkdir(const char *pathname, int mode)
{
        mode = mode+1; // make the compiler shutup about unused param
        /* Attempt to open the file */
        EFI_FILE *f;
        CHAR16 *wfname = (CHAR16 *)malloc((strlen(pathname) + 1) * sizeof(CHAR16));
        if(wfname == NULL)
        {
                errno = ENOMEM;
                return -1;
        }
        mbstowcs((wchar_t *)wfname, pathname, strlen(pathname) + 1);

        /* Convert backslashes to forward slashes */
        conv_backslashes(wfname);

        EFI_STATUS s = fopen_root->Open(fopen_root, &f, wfname, EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
        free(wfname);
        if(s == EFI_NOT_FOUND)
        {
                fprintf(stderr, "efilibc: fopen(%s): EFI_NOT_FOUND\n", pathname);
                errno = ENOENT;
                return -1;
        }
        else if(EFI_ERROR(s))
        {
                fprintf(stderr, "efilibc: fopen(): error %i\n", s);
                errno = EFAULT;
                return -1;
        }
        f->Close(f);
        return 0;
}

ZOIDBERG_FILE *zoidberg_fopen(const char *path, const char *mode)
{
	if(fopen_root == NULL)
	{
		errno = EFAULT;
		fprintf(stderr, "efilibc: fopen(): error: please call efilibc_init() before fopen()\n");
		return NULL;
	}

	if(next_fileno >= MAX_FILENO)
	{
		errno = EMFILE;
		return NULL;
	}

	/* Interpret mode (simplistic) */
	if(mode == NULL)
	{
		errno = EINVAL;
		return NULL;
	}

	UINT64 openmode;
	if(mode[0] == 'r')
	{
		if(mode[1] == '+')
			openmode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;
		else
			openmode = EFI_FILE_MODE_READ;
	} else
		openmode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;

	/* Attempt to open the file */
	EFI_FILE *f;
	CHAR16 *wfname = (CHAR16 *)malloc((strlen(path) + 1) * sizeof(CHAR16));
	if(wfname == NULL)
	{
		errno = ENOMEM;
		return NULL;
	}
	mbstowcs((wchar_t *)wfname, path, strlen(path) + 1);

	/* Convert backslashes to forward slashes */
	conv_backslashes(wfname);

	EFI_STATUS s = fopen_root->Open(fopen_root, &f, wfname, openmode, 0);
	free(wfname);
	if(s == EFI_NOT_FOUND)
	{
		fprintf(stderr, "efilibc: fopen(%s): EFI_NOT_FOUND\n", path);
		errno = ENOENT;
		return NULL;
	}
	else if(EFI_ERROR(s))
	{
		fprintf(stderr, "efilibc: fopen(): error %i\n", s);
		errno = EFAULT;
		return NULL;
	}
	ZOIDBERG_FILE *ret = (ZOIDBERG_FILE *)malloc(sizeof(FILE));
	assert(ret);
	memset(ret, 0, sizeof(ZOIDBERG_FILE));
	ret->f = f;
	ret->fileno = next_fileno++;

	fileno_map[ret->fileno] = ret;
	return ret;
}

size_t zoidberg_fread(void *ptr, size_t size, size_t nmemb, ZOIDBERG_FILE *stream)
{
	if(stream == zoidberg_stdin)
		return stdin_fread(ptr, size, nmemb, stdin_data);
	else if((stream == zoidberg_stdout) || (stream == zoidberg_stderr))
		return 0;

	if((stream == NULL) || (ptr == NULL))
	{
		errno = EINVAL;
		return 0;
	}

	UINTN buf_size = size * nmemb;
	UINTN old_buf_size = buf_size;
	EFI_STATUS s = stream->f->Read(stream->f, &buf_size, ptr);
	if(EFI_ERROR(s))
		return 0;

	if(buf_size != old_buf_size)
		stream->eof = 1;

	return buf_size / size;
}

size_t zoidberg_fwrite(void *ptr, size_t size, size_t nmemb, ZOIDBERG_FILE *stream)
{
	if(stream == zoidberg_stdin)
		return 0;
	else if(stream == zoidberg_stdout)
		return stdout_fwrite(ptr, size, nmemb, stdout_data);
	else if(stream == zoidberg_stderr)
		return stderr_fwrite(ptr, size, nmemb, stderr_data);

	if((stream == NULL) || (ptr == NULL))
	{
		errno = EINVAL;
		return 0;
	}

	UINTN buf_size = size * nmemb;
	UINTN old_buf_size = buf_size;
	EFI_STATUS s = stream->f->Write(stream->f, &buf_size, ptr);
	if(EFI_ERROR(s))
		return 0;

	if(buf_size != old_buf_size)
		stream->eof = 1;

	return buf_size / size;
}

int zoidberg_fgetc(ZOIDBERG_FILE *stream)
{
	char ret;
	if(zoidberg_fread(&ret, 1, 1, stream) != 1)
		return EOF;
	return (int)ret;
}

int zoidberg_fputc(int c, ZOIDBERG_FILE *stream)
{
	if(zoidberg_fwrite(&c, 1, 1, stream) != 1)
		return EOF;
	return c;
}

int zoidberg_fclose(ZOIDBERG_FILE *stream)
{
	if((stream == NULL) || (stream == zoidberg_stdin) || (stream == zoidberg_stdout) || (stream == zoidberg_stderr))
	{
		errno = EBADF;
		return EOF;
	}

	stream->f->Close(stream->f);
	fileno_map[stream->fileno] = NULL;
	free(stream);

	return 0;
}

void zoidberg_clearerr(ZOIDBERG_FILE *stream)
{
	if((stream == NULL) || (stream == zoidberg_stdin) || (stream == zoidberg_stdout) || (stream == zoidberg_stderr))
		return;
	stream->error = 0;
	stream->eof = 0;
}

int zoidberg_feof(ZOIDBERG_FILE *stream)
{
	if((stream == NULL) || (stream == zoidberg_stdin))
		return 0;
	if((stream == zoidberg_stdout) || (stream == zoidberg_stderr))
		return 1;
	return stream->eof;
}

int zoidberg_ferror(ZOIDBERG_FILE *stream)
{
	if(stream == NULL)
		return 1;
	if((stream == zoidberg_stdin) || (stream == zoidberg_stdout) || (stream == zoidberg_stderr))
		return 0;
	return stream->error;
}

int zoidberg_fileno(ZOIDBERG_FILE *stream)
{
	return stream->fileno;
}

int zoidberg_isatty(int fd)
{
	if((fd < 0) || (fd > MAX_FILENO))
	{
		errno = EBADF;
		return 0;
	}

	if((fd == 0) || (fd == 1) || (fd == 2))
		return 1;

	if(fileno_map[fd] == NULL)
	{
		errno = EBADF;
		return 0;
	}

	int ret = fileno_map[fd]->istty;
	if(ret)
		return ret;
	else
	{
		errno = EINVAL;
		return ret;
	}
}

int zoidberg_fseek(ZOIDBERG_FILE *stream, long pos, int whence)
{
	if((stream == zoidberg_stdin) || (stream == zoidberg_stdout) || (stream == zoidberg_stderr) || (stream == NULL))
	{
		errno = EBADF;
		return -1;
	}

	UINT64 new_pos;
	EFI_STATUS s;
	switch(whence)
	{
		case SEEK_SET:
			new_pos = (UINT64)pos;
			break;
		case SEEK_CUR:
			{
				UINT64 cur_pos;
				s = stream->f->GetPosition(stream->f, &cur_pos);
				if(EFI_ERROR(s))
				{
					errno = EFAULT;
					return -1;
				}
				if(pos < 0)
					new_pos = cur_pos - (UINT64)(-pos);
				else
					new_pos = cur_pos + (UINT64)pos;
			}
			break;
		case SEEK_END:
			new_pos = fsize(stream) + (UINT64)pos;
			break;
		default:
			errno = EINVAL;
			return -1;
	}

	s = stream->f->SetPosition(stream->f, new_pos);
	if(EFI_ERROR(s))
	{
		errno = EFAULT;
		return -1;
	}
	stream->eof = 0;
	return 0;
}

void zoidberg_rewind(ZOIDBERG_FILE *stream)
{
	if(zoidberg_fseek(stream, 0, SEEK_SET) == 0)
		stream->error = 0;
}

long zoidberg_ftell(ZOIDBERG_FILE *stream)
{
	if((stream == zoidberg_stdin) || (stream == zoidberg_stdout) || (stream == zoidberg_stderr) || (stream == NULL))
	{
		errno = EBADF;
		return -1;
	}

	UINT64 pos;
	EFI_STATUS s = stream->f->GetPosition(stream->f, &pos);
	if(EFI_ERROR(s))
	{
		errno = EFAULT;
		return -1;
	}
	return (long)pos;
}

int zoidberg_fgetpos(ZOIDBERG_FILE *stream, fpos_t *pos)
{
	long p = zoidberg_ftell(stream);
	if(p == -1)
		return -1;
	
	*pos = p;
	return 0;
}

int zoidberg_fsetpos(ZOIDBERG_FILE *stream, fpos_t *pos)
{
	return zoidberg_fseek(stream, *pos, SEEK_SET);
}

int zoidberg_fflush(ZOIDBERG_FILE *stream)
{
	if((stream == zoidberg_stdin) || (stream == NULL))
	{
		errno = EBADF;
		return EOF;
	}

	if((stream == zoidberg_stderr) || (stream == zoidberg_stdout))
		return 0;

	EFI_STATUS s = stream->f->Flush(stream->f);
	if(EFI_ERROR(s))
	{
		fprintf(stderr, "efilibc: fflush failed: %i\n", s);
		errno = EFAULT;
		return EOF;
	}

	return 0;
}

int zoidberg_remove(const char *pathname)
{
	if(pathname == NULL)
	{
		errno = EINVAL;
		return -1;
	}

	ZOIDBERG_FILE *f = zoidberg_fopen(pathname, "r");
	if(f == NULL)
		return -1;

	EFI_STATUS s = f->f->Delete(f->f);

	fileno_map[f->fileno] = NULL;
	free(f);

	if(s == EFI_WARN_DELETE_FAILURE)
	{
		fprintf(stderr, "efilibc: remove: could not remove %s\n", pathname);
		errno = EFAULT;
		return -1;
	}

	return 0;
}

int f_is_dir(ZOIDBERG_FILE *stream)
{
        UINTN buf_size = 0;
        EFI_STATUS s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, NULL);
        if(s != EFI_BUFFER_TOO_SMALL)
        {
                errno = EFAULT;
                return -1;
        }
        EFI_FILE_INFO *fi = (EFI_FILE_INFO *)malloc(buf_size);
        if(fi == NULL)
        {
                errno = ENOMEM;
                return -1;
        }
        s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, fi);
        if(EFI_ERROR(s))
        {
                free(fi);
                errno = EFAULT;
                return -1;
        }
        UINT64 attr_vals = fi->Attribute;
        int retval=0;
        if((attr_vals & EFI_FILE_DIRECTORY) != 0) {
          retval=1;
        }
        free(fi);
        return retval;
}

#define isleap(y)	(((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#define SECSPERHOUR ( 60*60 )
#define SECSPERDAY	(24 * SECSPERHOUR)
time_t
efi_time(EFI_TIME *ETime)
{
    /*
    //  These arrays give the cumulative number of days up to the first of the
    //  month number used as the index (1 -> 12) for regular and leap years.
    //  The value at index 13 is for the whole year.
    */
    static time_t CumulativeDays[2][14] = {
    {0,
     0,
     31,
     31 + 28,
     31 + 28 + 31,
     31 + 28 + 31 + 30,
     31 + 28 + 31 + 30 + 31,
     31 + 28 + 31 + 30 + 31 + 30,
     31 + 28 + 31 + 30 + 31 + 30 + 31,
     31 + 28 + 31 + 30 + 31 + 30 + 31 + 31,
     31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
     31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
     31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
     31 + 28 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31 },
    {0,
     0,
     31,
     31 + 29,
     31 + 29 + 31,
     31 + 29 + 31 + 30,
     31 + 29 + 31 + 30 + 31,
     31 + 29 + 31 + 30 + 31 + 30,
     31 + 29 + 31 + 30 + 31 + 30 + 31,
     31 + 29 + 31 + 30 + 31 + 30 + 31 + 31,
     31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30,
     31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31,
     31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30,
     31 + 29 + 31 + 30 + 31 + 30 + 31 + 31 + 30 + 31 + 30 + 31 }};

    time_t  UTime; 
    int     Year;

    /*
    //  Do a santity check
    */
    if ( ETime->Year  <  1998 || ETime->Year   > 2099 ||
    	 ETime->Month ==    0 || ETime->Month  >   12 ||
    	 ETime->Day   ==    0 || ETime->Month  >   31 ||
    	                         ETime->Hour   >   23 ||
    	                         ETime->Minute >   59 ||
    	                         ETime->Second >   59 ||
    	 ETime->TimeZone  < -1440                     ||
    	 (ETime->TimeZone >  1440 && ETime->TimeZone != 2047) ) {
    	return (0);
    }

    /*
    // Years
    */
    UTime = 0;
    for (Year = 1970; Year != ETime->Year; ++Year) {
        UTime += (CumulativeDays[isleap(Year)][13] * SECSPERDAY);
    }

    /*
    // UTime should now be set to 00:00:00 on Jan 1 of the file's year.
    //
    // Months  
    */
    UTime += (CumulativeDays[isleap(ETime->Year)][ETime->Month] * SECSPERDAY);

    /*
    // UTime should now be set to 00:00:00 on the first of the file's month and year
    //
    // Days -- Don't count the file's day
    */
    UTime += (((ETime->Day > 0) ? ETime->Day-1:0) * SECSPERDAY);

    /*
    // Hours
    */
    UTime += (ETime->Hour * SECSPERHOUR);

    /*
    // Minutes
    */
    UTime += (ETime->Minute * 60);

    /*
    // Seconds
    */
    UTime += ETime->Second;

    /*
    //  EFI time is repored in local time.  Adjust for any time zone offset to
    //  get true UT
    */
    if ( ETime->TimeZone != EFI_UNSPECIFIED_TIMEZONE ) {
    	/*
    	//  TimeZone is kept in minues...
    	*/
    	UTime += (ETime->TimeZone * 60);
    }
    
    return UTime;
}

time_t f_mod_time(ZOIDBERG_FILE* stream)
{
 UINTN buf_size = 0;
        EFI_STATUS s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, NULL);
        if(s != EFI_BUFFER_TOO_SMALL)
        {
                errno = EFAULT;
                return -1;
        }
        EFI_FILE_INFO *fi = (EFI_FILE_INFO *)malloc(buf_size);
        if(fi == NULL)
        {
                errno = ENOMEM;
                return -1;
        }
        s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, fi);
        if(EFI_ERROR(s))
        {
                free(fi);
                errno = EFAULT;
                return -1;
        }
        EFI_TIME t = fi->ModificationTime;
        free(fi);
        return efi_time(&t);
}


#ifndef POSIXLY_CORRECT
long zoidberg_fsize(ZOIDBERG_FILE *stream)
{
	UINTN buf_size = 0;
	EFI_STATUS s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, NULL);
	if(s != EFI_BUFFER_TOO_SMALL)
	{
		errno = EFAULT;
		return -1;
	}
	EFI_FILE_INFO *fi = (EFI_FILE_INFO *)malloc(buf_size);
	if(fi == NULL)
	{
		errno = ENOMEM;
		return -1;
	}
	s = stream->f->GetInfo(stream->f, &GenericFileInfo, &buf_size, fi);
	if(EFI_ERROR(s))
	{
		free(fi);
		errno = EFAULT;
		return -1;
	}
	long len = (long)fi->FileSize;
	free(fi);
	return len;
}


#endif
