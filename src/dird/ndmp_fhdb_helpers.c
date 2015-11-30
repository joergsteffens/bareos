/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2015-2015 Planets Communications B.V.
   Copyright (C) 2015-2015 Bareos GmbH & Co. KG

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
/*
 * FHDB helper routines for NDMP Data Management Application (DMA)
 *
 * Marco van Wieringen, May 2015
 */

#include "bareos.h"
#include "dird.h"

#if HAVE_NDMP

#include "ndmp/ndmagents.h"

/*
 * Store all entries in the FHDB as hardlinked items to the NDMP archive in the backup catalog.
 */
void ndmp_store_attribute_record(JCR *jcr, char *fname, char *linked_fname,
                                 char *attributes, int8_t FileType, uint64_t Node, uint64_t Offset)
{
   ATTR_DBR *ar;

   ar = jcr->ar;
   if (jcr->cached_attribute) {
      Dmsg2(400, "Cached attr. Stream=%d fname=%s\n", ar->Stream, ar->fname);
      if (!db_create_attributes_record(jcr, jcr->db, ar)) {
         Jmsg1(jcr, M_FATAL, 0, _("Attribute create error: ERR=%s"), db_strerror(jcr->db));
         return;
      }
      jcr->cached_attribute = false;
   }

   /*
    * We only update some fields of this structure the rest is already filled
    * before by initial attributes saved by the tape agent in the storage daemon.
    */
   jcr->ar->fname = fname;
   jcr->ar->link = linked_fname;
   jcr->ar->attr = attributes;
   jcr->ar->Stream = STREAM_UNIX_ATTRIBUTES;
   jcr->ar->FileType = FileType;

   if (!db_create_attributes_record(jcr, jcr->db, ar)) {
      Jmsg1(jcr, M_FATAL, 0, _("Attribute create error: ERR=%s"), db_strerror(jcr->db));
      return;
   }
}

void ndmp_convert_fstat(ndmp9_file_stat *fstat, int32_t FileIndex,
                        int8_t *FileType, POOL_MEM &attribs)
{
   struct stat statp;

   /*
    * Convert the NDMP file_stat structure into a UNIX one.
    */
   memset(&statp, 0, sizeof(statp));

   /*
    * If we got a valid mode of the file fill the UNIX stat struct.
    */
   if (fstat->mode.valid == NDMP9_VALIDITY_VALID) {
      switch (fstat->ftype) {
      case NDMP9_FILE_DIR:
         statp.st_mode = fstat->mode.value | S_IFDIR;
         *FileType = FT_DIREND;
         break;
      case NDMP9_FILE_FIFO:
         statp.st_mode = fstat->mode.value | S_IFIFO;
         *FileType = FT_FIFO;
         break;
      case NDMP9_FILE_CSPEC:
         statp.st_mode = fstat->mode.value | S_IFCHR;
         *FileType = FT_SPEC;
         break;
      case NDMP9_FILE_BSPEC:
         statp.st_mode = fstat->mode.value | S_IFBLK;
         *FileType = FT_SPEC;
         break;
      case NDMP9_FILE_REG:
         statp.st_mode = fstat->mode.value | S_IFREG;
         *FileType = FT_REG;
         break;
      case NDMP9_FILE_SLINK:
         statp.st_mode = fstat->mode.value | S_IFLNK;
         *FileType = FT_LNK;
         break;
      case NDMP9_FILE_SOCK:
         statp.st_mode = fstat->mode.value | S_IFSOCK;
         *FileType = FT_SPEC;
         break;
      case NDMP9_FILE_REGISTRY:
         statp.st_mode = fstat->mode.value | S_IFREG;
         *FileType = FT_REG;
         break;
      case NDMP9_FILE_OTHER:
         statp.st_mode = fstat->mode.value | S_IFREG;
         *FileType = FT_REG;
         break;
      default:
         break;
      }

      if (fstat->mtime.valid == NDMP9_VALIDITY_VALID) {
         statp.st_mtime = fstat->mtime.value;
      }

      if (fstat->atime.valid == NDMP9_VALIDITY_VALID) {
         statp.st_atime = fstat->atime.value;
      }

      if (fstat->ctime.valid == NDMP9_VALIDITY_VALID) {
         statp.st_ctime = fstat->ctime.value;
      }

      if (fstat->uid.valid == NDMP9_VALIDITY_VALID) {
         statp.st_uid = fstat->uid.value;
      }

      if (fstat->gid.valid == NDMP9_VALIDITY_VALID) {
         statp.st_gid = fstat->gid.value;
      }

      if (fstat->links.valid == NDMP9_VALIDITY_VALID) {
         statp.st_nlink = fstat->links.value;
      }
   }

   /*
    * Encode a stat structure into an ASCII string.
    */
   encode_stat(attribs.c_str(), &statp, sizeof(statp), FileIndex, STREAM_UNIX_ATTRIBUTES);
}
#endif
