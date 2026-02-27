/* Malloc huge Page support.  Linux implementation.
   Copyright (C) 2021-2026 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <https://www.gnu.org/licenses/>.  */

#include <dirent.h>
#include <malloc-hugepages.h>
#include <not-cancel.h>
#include <sys/mman.h>

unsigned long int
__malloc_default_thp_pagesize (void)
{
  if (MALLOC_DEFAULT_THP_PAGESIZE != 0)
    return MALLOC_DEFAULT_THP_PAGESIZE;

  return __hugepages_default_thp_pagesize ();
}

static size_t
malloc_default_hugepage_size (void)
{
  int fd = __open64_nocancel ("/proc/meminfo", O_RDONLY);
  if (fd == -1)
    return 0;

  size_t hpsize = 0;

  char buf[512];
  off64_t off = 0;
  while (1)
    {
      ssize_t r = __pread64_nocancel (fd, buf, sizeof (buf) - 1, off);
      if (r < 0)
	break;
      buf[r] = '\0';

      /* If the tag is not found, read the last line again.  */
      const char *s = strstr (buf, "Hugepagesize:");
      if (s == NULL)
	{
	  char *nl = strrchr (buf, '\n');
	  if (nl == NULL)
	    break;
	  off += (nl + 1) - buf;
	  continue;
	}

      /* The default huge page size is in the form:
	 Hugepagesize:       NUMBER kB  */
      s += sizeof ("Hugepagesize: ") - 1;
      for (int i = 0; (s[i] >= '0' && s[i] <= '9') || s[i] == ' '; i++)
	{
	  if (s[i] == ' ')
	    continue;
	  hpsize *= 10;
	  hpsize += s[i] - '0';
	}
      hpsize *= 1024;
      break;
    }

  __close_nocancel (fd);

  return hpsize;
}

static inline int
hugepage_flags (size_t pagesize)
{
  return MAP_HUGETLB | (__builtin_ctzll (pagesize) << MAP_HUGE_SHIFT);
}

void
__malloc_hugepage_config (size_t requested, size_t *pagesize, int *flags)
{
  *pagesize = 0;
  *flags = 0;

  if (requested == 0)
    {
      *pagesize = malloc_default_hugepage_size ();
      if (*pagesize != 0)
	*flags = hugepage_flags (*pagesize);
      return;
    }

  /* Each entry represents a supported huge page in the form of:
     hugepages-<size>kB.  */
  int dirfd = __open64_nocancel ("/sys/kernel/mm/hugepages",
				 O_RDONLY | O_DIRECTORY, 0);
  if (dirfd == -1)
    return;

  char buffer[1024];
  while (true)
    {
#if !IS_IN(libc)
# define __getdents64 getdents64
#endif
      ssize_t ret = __getdents64 (dirfd, buffer, sizeof (buffer));
      if (ret == -1)
	break;
      else if (ret == 0)
        break;

      bool found = false;
      char *begin = buffer, *end = buffer + ret;
      while (begin != end)
        {
          unsigned short int d_reclen;
          memcpy (&d_reclen, begin + offsetof (struct dirent64, d_reclen),
                  sizeof (d_reclen));
          const char *dname = begin + offsetof (struct dirent64, d_name);
          begin += d_reclen;

          if (dname[0] == '.'
	      || strncmp (dname, "hugepages-", sizeof ("hugepages-") - 1) != 0)
            continue;

	  size_t hpsize = 0;
	  const char *sizestr = dname + sizeof ("hugepages-") - 1;
	  for (int i = 0; sizestr[i] >= '0' && sizestr[i] <= '9'; i++)
	    {
	      hpsize *= 10;
	      hpsize += sizestr[i] - '0';
	    }
	  hpsize *= 1024;

	  if (hpsize == requested)
	    {
	      *pagesize = hpsize;
	      *flags = hugepage_flags (*pagesize);
	      found = true;
	      break;
	    }
        }
      if (found)
	break;
    }

  __close_nocancel (dirfd);
}
