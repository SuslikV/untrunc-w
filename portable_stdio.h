//MPLv2 or later
#ifndef PORTABLE_STDIO_H
#define PORTABLE_STDIO_H

#if defined(__WINDOWS__)

//libgw32c-0.4 (GPLv2.1 or later)
// for windows  fseeko >> fseek
//              ftello >> ftell
//              ftello64 >>
//              fseeko64 >>

#include <stdio.h>

int fseeko (FILE* fp, off_t offset, int whence)
{
  return fseek(fp, (long) offset, whence);
}

off_t ftello (FILE *fp)
{
  return (off_t) ftell(fp);
}

int fseeko64 (FILE* fp, int64_t offset, int whence)
{
  fpos_t pos;
  if (whence == SEEK_CUR) {
    fgetpos(fp, &pos);
    pos += (fpos_t) offset;
  }
  else if (whence == SEEK_END)
    pos = (fpos_t) (_filelengthi64(fileno(fp)) + offset);
  else if (whence == SEEK_SET)
    pos = (fpos_t) offset;
  return fsetpos(fp, &pos);
}

int64_t ftello64 (FILE *fp)
{
  fpos_t pos;
  if (fgetpos(fp, &pos))
    return (int64_t) -1LL;
  else
    return ((int64_t) pos);
}

#endif

#endif // PORTABLE_STDIO_H
