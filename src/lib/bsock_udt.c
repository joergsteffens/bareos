/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2013-2013 Bareos GmbH & Co. KG

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
 * UDT Sockets abstraction.
 *
 * Marco van Wieringen, September 2013.
 */

#include "bareos.h"
#include "jcr.h"
#include "bsock_udt.h"

BSOCK_UDT::BSOCK_UDT()
{
   init();
}

BSOCK_UDT::~BSOCK_UDT()
{
   destroy();
}

void BSOCK_UDT::init()
{
   /*
    * We cannot memset the class as then we would kill the virtual member pointers etc.
    */
   m_fd = -1;
   read_seqno = 0;
   msg = get_pool_memory(PM_BSOCK);
   errmsg = get_pool_memory(PM_MESSAGE);
   res = NULL;
   m_spool_fd = NULL;
   tls = NULL;
   src_addr = NULL;
   in_msg_no = 0;
   out_msg_no = 0;
   msglen = 0;
   timer_start = 0;
   b_errno = 0;
   m_blocking = true;
   errors = 0;
   m_suppress_error_msgs = false;
   memset(&client_addr, 0, sizeof(client_addr));
   memset(&peer_addr, 0, sizeof(peer_addr));
   m_who = NULL;
   m_host = NULL;
   m_port = 0;
   m_tid = 0;
   m_data_end = 0;
   m_FileIndex = 0;
   m_timed_out = false;
   m_terminated = false;
   m_cloned = false;
   m_spool = false;
   m_use_locking = false;
   m_use_bursting = false;
   m_bwlimit = 0;
   m_nb_bytes = 0;
   m_last_tick = 0;

   /*
    * ****FIXME**** reduce this to a few hours once heartbeats are implemented
    */
   timeout = 60 * 60 * 6 * 24;   /* 6 days timeout */
}

BSOCK *BSOCK_UDT::clone()
{
   BSOCK_UDT *clone;
   POOLMEM *o_msg, *o_errmsg;

   clone = New(BSOCK_UDT);

   /*
    * Copy the data from the original BSOCK but preserve the msg and errmsg buffers.
    */
   o_msg = clone->msg;
   o_errmsg = clone->errmsg;
   memcpy(clone, this, sizeof(BSOCK_TCP));
   clone->msg = o_msg;
   clone->errmsg = o_errmsg;

   if (m_who) {
      clone->set_who(bstrdup(m_who));
   }
   if (m_host) {
      clone->set_who(bstrdup(m_host));
   }
   if (src_addr) {
      clone->src_addr = New(IPADDR(*(src_addr)));
   }
   m_cloned = true;

   return (BSOCK *)clone;
}

/*
 * Try to connect to host for max_retry_time at retry_time intervals.
 * Note, you must have called the constructor prior to calling
 * this routine.
 */
bool BSOCK_UDT::connect(JCR * jcr, int retry_interval, utime_t max_retry_time,
                        utime_t heart_beat, const char *name, char *host,
                        char *service, int port, bool verbose)
{
   return false;
}

/*
 * Finish initialization of the pocket structure.
 */
void BSOCK_UDT::fin_init(JCR * jcr, int sockfd, const char *who, const char *host,
                         int port, struct sockaddr *lclient_addr)
{
}

/*
 * Open a UDT connection to the server
 * Returns NULL
 * Returns BSOCK * pointer on success
 */
bool BSOCK_UDT::open(JCR *jcr, const char *name, char *host, char *service,
                     int port, utime_t heart_beat, int *fatal)
{
   return false;
}

/*
 * Send a message over the network. The send consists of
 * two network packets. The first is sends a 32 bit integer containing
 * the length of the data packet which follows.
 *
 * Returns: false on failure
 *          true  on success
 */
bool BSOCK_UDT::send()
{
   return false;
}

/*
 * Receive a message from the other end. Each message consists of
 * two packets. The first is a header that contains the size
 * of the data that follows in the second packet.
 * Returns number of bytes read (may return zero)
 * Returns -1 on signal (BNET_SIGNAL)
 * Returns -2 on hard end of file (BNET_HARDEOF)
 * Returns -3 on error  (BNET_ERROR)
 *
 *  Unfortunately, it is a bit complicated because we have these
 *    four return types:
 *    1. Normal data
 *    2. Signal including end of data stream
 *    3. Hard end of file
 *    4. Error
 *  Using is_bnet_stop() and is_bnet_error() you can figure this all out.
 */
int32_t BSOCK_UDT::recv()
{
   return -1;
}

int BSOCK_UDT::get_peer(char *buf, socklen_t buflen)
{
   return -1;
}

/*
 * Set the network buffer size, suggested size is in size.
 * Actual size obtained is returned in bs->msglen
 *
 * Returns: false on failure
 *          true  on success
 */
bool BSOCK_UDT::set_buffer_size(uint32_t size, int rw)
{
   return false;
}

/*
 * Set socket non-blocking
 * Returns previous socket flag
 */
int BSOCK_UDT::set_nonblocking()
{
   return -1;
}

/*
 * Set socket blocking
 * Returns previous socket flags
 */
int BSOCK_UDT::set_blocking()
{
   return -1;
}

/*
 * Restores socket flags
 */
void BSOCK_UDT::restore_blocking (int flags)
{
}

/*
 * Wait for a specified time for data to appear on
 * the BSOCK connection.
 *
 *   Returns: 1 if data available
 *            0 if timeout
 *           -1 if error
 */
int BSOCK_UDT::wait_data(int sec, int usec)
{
   return -1;
}

/*
 * As above, but returns on interrupt
 */
int BSOCK_UDT::wait_data_intr(int sec, int usec)
{
   return -1;
}

void BSOCK_UDT::close()
{
}

void BSOCK_UDT::destroy()
{
}

/*
 * Read a nbytes from the network.
 * It is possible that the total bytes require in several
 * read requests
 */
int32_t BSOCK_UDT::read_nbytes(char *ptr, int32_t nbytes)
{
   return -1;
}

/*
 * Write nbytes to the network.
 * It may require several writes.
 */
int32_t BSOCK_UDT::write_nbytes(char *ptr, int32_t nbytes)
{
   return -1;
}
