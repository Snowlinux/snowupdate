/* hook-remove.c - a simple application that can remove hook files from
 *                 the file system
 * Copyright (C) 2005 Canonical
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor
 * Boston, MA  02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define HOOK_PATH "/var/lib/update-notifier/user.d"

int main(int argc, char *argv[])
{
   int i;

   if(chdir(HOOK_PATH) < 0)
   {
      perror("chdir");
      exit(1);
   }

   if(chroot(HOOK_PATH) < 0) 
   {
      perror("chroot");
      exit(1);
   }
   
   for(i=1; i<argc; i++)
   {
      unlink(argv[i]);
   }
   exit(0);
}
