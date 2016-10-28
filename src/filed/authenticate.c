/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2000-2010 Free Software Foundation Europe e.V.
   Copyright (C) 2011-2012 Planets Communications B.V.
   Copyright (C) 2013-2016 Bareos GmbH & Co. KG

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
 * Authenticate Director who is attempting to connect.
 *
 * Kern Sibbald, October 2000
 */

#include "bareos.h"
#include "filed.h"

const int dbglvl = 50;

/* Version at end of Hello
 *   prior to 10Mar08 no version
 *   1 10Mar08
 *   2 13Mar09 - Added the ability to restore from multiple storages
 *   3 03Sep10 - Added the restore object command for vss plugin 4.0
 *   4 25Nov10 - Added bandwidth command 5.1
 *   5 24Nov11 - Added new restore object command format (pluginname) 6.0
 *
 *  51 21Mar13 - Added reverse datachannel initialization
 *  52 13Jul13 - Added plugin options
 *  53 02Apr15 - Added setdebug timestamp
 *  54 29Oct15 - Added getSecureEraseCmd
 */
static char OK_hello_compat[] =
   "2000 OK Hello 5\n";
static char OK_hello[] =
   "2000 OK Hello 54\n";

static char Dir_sorry[] =
   "2999 Authentication failed.\n";

/*
 * To prevent DOS attacks,
 * wait a bit in case of an
 * authentication failure of a (remotely) initiated connection.
 */
static inline void delay()
{
   static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

   /*
    * Single thread all failures to avoid DOS
    */
   P(mutex);
   bmicrosleep(6, 0);
   V(mutex);
}

static inline void authenticate_failed(JCR *jcr, POOL_MEM &message)
{
   Dmsg0(dbglvl, message.c_str());
   Jmsg0(jcr, M_FATAL, 0, message.c_str());
   delay();
}

/*
 * Inititiate the communications with the Director.
 * He has made a connection to our server.
 *
 * Basic tasks done here:
 * We read Director's initial message and authorize him.
 */
bool authenticate_director(JCR *jcr)
{
   BSOCK *dir = jcr->dir_bsock;

   POOL_MEM errormsg(PM_MESSAGE);
   POOL_MEM dirname(PM_MESSAGE);
   DIRRES *director = NULL;

   if (dir->msglen < 25 || dir->msglen > 500) {
      char addr[64];
      char *who = bnet_get_peer(dir, addr, sizeof(addr)) ? dir->who() : addr;
      errormsg.bsprintf(_("Bad Hello command from Director at %s. Len=%d.\n"),
                           who, dir->msglen);
      authenticate_failed(jcr, errormsg);
      return false;
   }

   if (sscanf(dir->msg, "Hello Director %s calling", dirname.check_size(dir->msglen)) != 1) {
      char addr[64];
      char *who = bnet_get_peer(dir, addr, sizeof(addr)) ? dir->who() : addr;
      dir->msg[100] = 0;
      errormsg.bsprintf(_("Bad Hello command from Director at %s: %s\n"), who, dir->msg);
      authenticate_failed(jcr, errormsg);
      return false;
   }

   unbash_spaces(dirname.c_str());
   director = (DIRRES *)GetResWithName(R_DIRECTOR, dirname.c_str());

   if (!director) {
      char addr[64];
      char *who = bnet_get_peer(dir, addr, sizeof(addr)) ? dir->who() : addr;
      errormsg.bsprintf(_("Connection from unknown Director %s at %s rejected.\n"), dirname.c_str(), who);
      authenticate_failed(jcr, errormsg);
      return false;
   }

   if (!director->conn_from_dir_to_fd) {
      errormsg.bsprintf(_("Connection from Director %s rejected.\n"), dirname.c_str());
      authenticate_failed(jcr, errormsg);
      return false;
   }

   if (!dir->authenticate_inbound_connection(jcr, "Director",
                                             dirname.c_str(),
                                             director->password, director->tls)) {
      dir->fsend("%s", Dir_sorry);
      errormsg.bsprintf(_("Unable to authenticate Director %s.\n"), dirname.c_str());
      authenticate_failed(jcr, errormsg);
      return false;
   }

   jcr->director = director;

   return dir->fsend("%s", (me->compatible) ? OK_hello_compat : OK_hello);
}

/*
 * Authenticate with a remote director.
 */
bool authenticate_with_director(JCR *jcr, DIRRES *dir_res)
{
   BSOCK *dir = jcr->dir_bsock;

   return dir->authenticate_outbound_connection(jcr, "Director",
                                                dir_res->name(), dir_res->password,
                                                dir_res->tls);
}

/*
 * Authenticate a remote storage daemon.
 */
bool authenticate_storagedaemon(JCR *jcr)
{
   bool result = false;
   BSOCK *sd = jcr->store_bsock;
   s_password password;

   password.encoding = p_encoding_md5;
   password.value = jcr->sd_auth_key;

   result = sd->authenticate_inbound_connection(jcr, "Storage daemon", "", password, me->tls);

   /*
    * Destroy session key
    */
   memset(jcr->sd_auth_key, 0, strlen(jcr->sd_auth_key));
   if (!result) {
      delay();
   }

   return result;
}

/*
 * Authenticate with a remote storage daemon.
 */
bool authenticate_with_storagedaemon(JCR *jcr)
{
   bool result = false;
   BSOCK *sd = jcr->store_bsock;
   s_password password;

   password.encoding = p_encoding_md5;
   password.value = jcr->sd_auth_key;

   result = sd->authenticate_outbound_connection(jcr, "Storage daemon", "", password, me->tls);

   /*
    * Destroy session key
    */
   memset(jcr->sd_auth_key, 0, strlen(jcr->sd_auth_key));

   return result;
}
