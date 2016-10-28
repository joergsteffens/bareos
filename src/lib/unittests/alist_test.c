/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2003-2011 Free Software Foundation Europe e.V.
   Copyright (C) 2015-2016 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
/* originally was Kern Sibbald, June MMIII */
/*
 * extracted the TEST_PROGRAM functionality from the files in ..
 * and adapted for unittest framework cmocka
 *
 * Philipp Storz, April 2015
 */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

extern "C" {
#include <cmocka.h>
}

#include "bareos.h"

struct FILESET {
   alist mylist;
};

/*
 * helper functions
 */

void alist_fill(alist *list, int max)
{
   char buf[30];
   int start = 0;

   if (list) {
      start = list->size();
   }

   /*
    * fill list with strings of numbers from 0 to n.
    */
   for (int i=0; i<max; i++) {
      sprintf(buf, "%d", start+i);
      list->append(bstrdup(buf));
   }

   /*
    * verify that max elements have been added
    */
   assert_int_equal(list->size(), start + max);
}

/*
 * we expect, that the list is filled with strings of numbers from 0 to n.
 */
void test_foreach_alist(alist *list)
{
   char *str = NULL;
   char buf[30];
   int i=0;

   /*
    * test all available foreach loops
    */

   foreach_alist(str, list) {
      sprintf(buf, "%d", i);
      assert_string_equal(str, buf);
      i++;
   }

   foreach_alist_index(i, str, list) {
      sprintf(buf, "%d", i);
      assert_string_equal(str, buf);
   }

   foreach_alist_rindex(i, str, list) {
      sprintf(buf, "%d", i);
      assert_string_equal(str, buf);
   }
}

/*
 * Tests
 */

void test_alist_init_destroy()
{
   FILESET *fileset;
   fileset = (FILESET *)malloc(sizeof(FILESET));
   memset(fileset, 0, sizeof(FILESET));
   fileset->mylist.init();

   alist_fill(&(fileset->mylist), 20);
   for (int i=0; i< fileset->mylist.size(); i++) {
      assert_int_equal(i, atoi((char *)fileset->mylist[i]));
   }
   fileset->mylist.destroy();
   free(fileset);
}



void test_alist_dynamic() {
   alist *list=NULL;
   char *buf;

   // NULL->size() will segfault
   //assert_int_equal(list->size(), 0);

   // does foreach work for NULL?
   test_foreach_alist(list);

   // create empty list, which is prepared for a number of entires
   list = New(alist(10));
   assert_int_equal(list->size(), 0);

   // does foreach work for empty lists?
   test_foreach_alist(list);

   // fill the list
   alist_fill(list, 20);
   test_foreach_alist(list);

   // verify and remove the latest entries
   assert_int_equal(list->size(), 20);
   buf = (char *)list->pop();
   assert_string_equal(buf, "19");
   free(buf);

   assert_int_equal(list->size(), 19);
   buf = (char *)list->pop();
   assert_string_equal(buf, "18");
   free(buf);

   // added more entires
   alist_fill(list, 20);
   test_foreach_alist(list);

   assert_int_equal(list->size(), 38);

   delete(list);
}


/*
 * main entry point
 */
void test_alist(void **state) {
   (void) state; /* unused */

   test_alist_init_destroy();
   test_alist_dynamic();

   sm_dump(false);
}
