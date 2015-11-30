/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2011-2015 Planets Communications B.V.
   Copyright (C) 2013-2015 Bareos GmbH & Co. KG

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
 * Restore specific NDMP Data Management Application (DMA) routines
 *
 * Marco van Wieringen, May 2015
 */

#include "bareos.h"
#include "dird.h"

#if HAVE_NDMP

#define NDMP_NEED_ENV_KEYWORDS 1

#include "ndmp/ndmagents.h"
#include "ndmp_dma_priv.h"

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* Imported variables */

/* Forward referenced functions */

static char OKbootstrap[] =
   "3000 OK bootstrap\n";

/*
 * Walk the tree of selected files for restore and lookup the
 * correct fileid. Return the actual full pathname of the file
 * corresponding to the given fileid.
 */
static inline char *lookup_fileindex(JCR *jcr, int32_t FileIndex)
{
   TREE_NODE *node, *parent;
   POOL_MEM restore_pathname, tmp;

   node = first_tree_node(jcr->restore_tree_root);
   while (node) {
      /*
       * See if this is the wanted FileIndex.
       */
      if (node->FileIndex == FileIndex) {
         pm_strcpy(restore_pathname, node->fname);

         /*
          * Walk up the parent until we hit the head of the list.
          */
         for (parent = node->parent; parent; parent = parent->parent) {
            pm_strcpy(tmp, restore_pathname.c_str());
            Mmsg(restore_pathname, "%s/%s", parent->fname, tmp.c_str());
         }

         if (bstrncmp(restore_pathname.c_str(), "/@NDMP/", 7)) {
            return bstrdup(restore_pathname.c_str());
         }
      }

      node = next_tree_node(node);
   }

   return NULL;
}

/*
 * Add a filename to the files we want to restore.
 *
 * The RFC says this:
 *
 * original_path - The original path name of the data to be recovered,
 *                 relative to the backup root. If original_path is the null
 *                 string, the server shall recover all data contained in the
 *                 backup image.
 *
 * destination_path, name, other_name
 *               - Together, these identify the absolute path name to which
 *                 data are to be recovered.
 *
 *               If name is the null string:
 *                 - destination_path identifies the name to which the data
 *                   identified by original_path are to be recovered.
 *                 - other_name must be the null string.
 *
 *               If name is not the null string:
 *                 - destination_path, when concatenated with the server-
 *                   specific path name delimiter and name, identifies the
 *                   name to which the data identified by original_path are
 *                   to be recovered.
 *
 *               If other_name is not the null string:
 *                 - destination_path, when concatenated with the server-
 *                   specific path name delimiter and other_name,
 *                   identifies the alternate name-space name of the data
 *                   to be recovered. The definition of such alternate
 *                   name-space is server-specific.
 *
 * Neither name nor other_name may contain a path name delimiter.
 *
 * Under no circumstance may destination_path be the null string.
 *
 * If intermediate directories that lead to the path name to
 * recover do not exist, the server should create them.
 */
static inline void add_to_namelist(struct ndm_job_param *job,
                                   char *filename,
                                   char *restore_prefix,
                                   char *name,
                                   char *other_name,
                                   int64_t node)
{
   ndmp9_name nl;
   POOL_MEM destination_path;

   memset(&nl, 0, sizeof(ndmp9_name));

   /*
    * See if the filename is an absolute pathname.
    */
   if (*filename == '\0') {
      pm_strcpy(destination_path, restore_prefix);
   } else if (*filename == '/') {
      Mmsg(destination_path, "%s%s", restore_prefix, filename);
   } else {
      Mmsg(destination_path, "%s/%s", restore_prefix, filename);
   }

   nl.original_path = filename;
   nl.destination_path = destination_path.c_str();
   nl.name = name;
   nl.other_name = other_name;
   nl.node = node;

   ndma_store_nlist(&job->nlist_tab, &nl);
}

/*
 * See in the tree with selected files what files were selected to be restored.
 */
static inline int set_files_to_restore(JCR *jcr, struct ndm_job_param *job,
                                       int32_t FileIndex, char *restore_prefix)
{
   int len;
   int cnt = 0;
   TREE_NODE *node, *parent;
   POOL_MEM restore_pathname, tmp;

   node = first_tree_node(jcr->restore_tree_root);
   while (node) {
      /*
       * See if this is the wanted FileIndex and the user asked to extract it.
       */
      if (node->FileIndex == FileIndex && node->extract) {
         pm_strcpy(restore_pathname, node->fname);

         /*
          * Walk up the parent until we hit the head of the list.
          */
         for (parent = node->parent; parent; parent = parent->parent) {
            pm_strcpy(tmp, restore_pathname.c_str());
            Mmsg(restore_pathname, "%s/%s", parent->fname, tmp.c_str());
         }

         /*
          * We only want to restore the non pseudo NDMP names e.g. not the full backup stream name.
          */
         if (!bstrncmp(restore_pathname.c_str(), "/@NDMP/", 7)) {
            /*
             * See if we need to strip the prefix from the filename.
             */
            len = strlen(restore_prefix);
            if (bstrncmp(restore_pathname.c_str(), restore_prefix, len)) {
               add_to_namelist(job,  restore_pathname.c_str() + len, restore_prefix,
                               (char *)"", (char *)"", NDMP_INVALID_U_QUAD);
            } else {
               add_to_namelist(job,  restore_pathname.c_str(), restore_prefix,
                               (char *)"", (char *)"", NDMP_INVALID_U_QUAD);
            }
            cnt++;
         }
      }

      node = next_tree_node(node);
   }

   return cnt;
}

/*
 * Database handler that handles the returned environment data for a given JobId.
 */
static int ndmp_env_handler(void *ctx, int num_fields, char **row)
{
   struct ndm_env_table *envtab;
   ndmp9_pval pv;

   if (row[0] && row[1]) {
      envtab = (struct ndm_env_table *)ctx;
      pv.name = row[0];
      pv.value = row[1];

      ndma_store_env_list(envtab, &pv);
   }

   return 0;
}

/*
 * Fill the NDMP restore environment table with the data for the data agent to act on.
 */
static inline bool fill_restore_environment(JCR *jcr,
                                            int32_t current_fi,
                                            struct ndm_job_param *job)
{
   int i;
   char *bp;
   ndmp9_pval pv;
   FILESETRES *fileset;
   char *restore_pathname,
        *original_pathname,
        *restore_prefix,
        *level;
   POOL_MEM tape_device;
   POOL_MEM destination_path;
   ndmp_backup_format_option *nbf_options;

   /*
    * See if we know this backup format and get it options.
    */
   nbf_options = ndmp_lookup_backup_format_options(job->bu_type);

   /*
    * Lookup the current fileindex and map it to an actual pathname.
    */
   restore_pathname = lookup_fileindex(jcr, current_fi);
   if (!restore_pathname) {
      return false;
   } else {
      /*
       * Skip over the /@NDMP prefix.
       */
      original_pathname = restore_pathname + 6;
   }

   /*
    * See if there is a level embedded in the pathname.
    */
   bp = strrchr(original_pathname, '%');
   if (bp) {
      *bp++ = '\0';
      level = bp;
   } else {
      level = NULL;
   }

   /*
    * Lookup the environment stack saved during the backup so we can restore it.
    */
   if (!db_get_ndmp_environment_string(jcr, jcr->db, &jcr->jr,
                                       ndmp_env_handler, &job->env_tab)) {
      /*
       * Fallback code try to build a environment stack that is good enough to
       * restore this NDMP backup. This is used when the data is not available in
       * the database when its either expired or when an old NDMP backup is restored
       * where the whole environment was not saved.
       */

      if (!nbf_options || nbf_options->uses_file_history) {
         /*
          * We asked during the NDMP backup to receive file history info.
          */
         pv.name = ndmp_env_keywords[NDMP_ENV_KW_HIST];
         pv.value = ndmp_env_values[NDMP_ENV_VALUE_YES];
         ndma_store_env_list(&job->env_tab, &pv);
      }

      /*
       * Tell the data agent what type of restore stream to expect.
       */
      pv.name = ndmp_env_keywords[NDMP_ENV_KW_TYPE];
      pv.value = job->bu_type;
      ndma_store_env_list(&job->env_tab, &pv);

      /*
       * Tell the data agent that this is a NDMP backup which uses a level indicator.
       */
      if (level) {
         pv.name = ndmp_env_keywords[NDMP_ENV_KW_LEVEL];
         pv.value = level;
         ndma_store_env_list(&job->env_tab, &pv);
      }

      /*
       * Tell the data engine what was backuped.
       */
      pv.name = ndmp_env_keywords[NDMP_ENV_KW_FILESYSTEM];
      pv.value = original_pathname;
      ndma_store_env_list(&job->env_tab, &pv);
   }

   /*
    * Lookup any meta tags that need to be added.
    */
   fileset = jcr->res.fileset;
   for (i = 0; i < fileset->num_includes; i++) {
      int j;
      char *item;
      INCEXE *ie = fileset->include_items[i];

      /*
       * Loop over each file = entry of the fileset.
       */
      for (j = 0; j < ie->name_list.size(); j++) {
         item = (char *)ie->name_list.get(j);

         /*
          * See if the original path matches.
          */
         if (bstrcasecmp(item, original_pathname)) {
            int k, l;
            FOPTS *fo;

            for (k = 0; k < ie->num_opts; k++) {
               fo = ie->opts_list[i];

               /*
                * Parse all specific META tags for this option block.
                */
               for (l = 0; l < fo->meta.size(); l++) {
                  ndmp_parse_meta_tag(&job->env_tab, (char *)fo->meta.get(l));
               }
            }
         }
      }
   }

   /*
    * See where to restore the data.
    */
   restore_prefix = NULL;
   if (jcr->where) {
      restore_prefix = jcr->where;
   } else {
      restore_prefix = jcr->res.job->RestoreWhere;
   }

   if (!restore_prefix) {
      return false;
   }

   /*
    * Tell the data engine where to restore.
    */
   if (nbf_options && nbf_options->restore_prefix_relative) {
      switch (*restore_prefix) {
      case '^':
         /*
          * Use the restore_prefix as an absolute restore prefix.
          * We skip the leading ^ that is the trigger for absolute restores.
          */
         pm_strcpy(destination_path, restore_prefix + 1);
         break;
      default:
         /*
          * Use the restore_prefix as an relative restore prefix.
          */
         if (strlen(restore_prefix) == 1 && *restore_prefix == '/') {
            pm_strcpy(destination_path, original_pathname);
         } else {
            pm_strcpy(destination_path, restore_prefix);
            pm_strcat(destination_path, original_pathname);
         }
      }
   } else {
      if (strlen(restore_prefix) == 1 && *restore_prefix == '/') {
         /*
          * Use the original pathname as restore prefix.
          */
         pm_strcpy(destination_path, original_pathname);
      } else {
         /*
          * Use the restore_prefix as an absolute restore prefix.
          */
         pm_strcpy(destination_path, restore_prefix);
      }
   }

   pv.name = ndmp_env_keywords[NDMP_ENV_KW_PREFIX];
   pv.value = destination_path.c_str();
   ndma_store_env_list(&job->env_tab, &pv);

   if (!nbf_options || nbf_options->needs_namelist) {
      if (set_files_to_restore(jcr, job, current_fi,
                               destination_path.c_str()) == 0) {
         /*
          * There is no specific filename selected so restore everything.
          */
         add_to_namelist(job, (char *)"", destination_path.c_str(),
                         (char *)"", (char *)"", NDMP_INVALID_U_QUAD);
      }
   }

   /*
    * If we have a paired storage definition we put the storage daemon
    * auth key and the filesystem into the tape device name of the
    * NDMP session. This way the storage daemon can link the NDMP
    * data and the normal save session together.
    */
   if (jcr->store_bsock) {
      Mmsg(tape_device, "%s@%s", jcr->sd_auth_key, restore_pathname + 6);
      job->tape_device = bstrdup(tape_device.c_str());
   }

   free(restore_pathname);
   return true;
}

/*
 * Extract any post backup statistics.
 */
static inline bool extract_post_restore_stats(JCR *jcr,
                                              struct ndm_session *sess)
{
   bool retval = true;
   struct ndmmedia *media;

   /*
    * See if an error was raised during the backup session.
    */
   if (sess->error_raised) {
      return false;
   }

   /*
    * See if there is any media error.
    */
   for (media = sess->control_acb->job.result_media_tab.head; media; media = media->next) {
      if (media->media_open_error ||
          media->media_io_error ||
          media->label_io_error ||
          media->label_mismatch ||
          media->fmark_error) {
         retval = false;
      }
   }

   /*
    * Update the Job statistics from the NDMP statistics.
    */
   jcr->JobBytes += sess->control_acb->job.bytes_read;
   jcr->JobFiles++;

   return retval;
}

/*
 * Setup a NDMP restore session.
 */
bool do_ndmp_restore_init(JCR *jcr)
{
   free_wstorage(jcr);                /* we don't write */

   if (!jcr->restore_tree_root) {
      Jmsg(jcr, M_FATAL, 0, _("Cannot NDMP restore without a file selection.\n"));
      return false;
   }

   return true;
}

static inline int ndmp_wait_for_job_termination(JCR *jcr)
{
   jcr->setJobStatus(JS_Running);

   /*
    * Force cancel in SD if failing, but not for Incomplete jobs
    * so that we let the SD despool.
    */
   Dmsg4(100, "cancel=%d FDJS=%d JS=%d SDJS=%d\n",
         jcr->is_canceled(), jcr->FDJobStatus,
         jcr->JobStatus, jcr->SDJobStatus);
   if (jcr->is_canceled() || (!jcr->res.job->RescheduleIncompleteJobs)) {
      Dmsg3(100, "FDJS=%d JS=%d SDJS=%d\n",
            jcr->FDJobStatus, jcr->JobStatus, jcr->SDJobStatus);
      cancel_storage_daemon_job(jcr);
   }

   /*
    * Note, the SD stores in jcr->JobFiles/ReadBytes/JobBytes/JobErrors
    */
   wait_for_storage_daemon_termination(jcr);

   jcr->FDJobStatus = JS_Terminated;
   if (jcr->JobStatus != JS_Terminated) {
      return jcr->JobStatus;
   }
   if (jcr->FDJobStatus != JS_Terminated) {
      return jcr->FDJobStatus;
   }
   return jcr->SDJobStatus;
}

/*
 * The bootstrap is stored in a file, so open the file, and loop
 * through it processing each storage device in turn. If the
 * storage is different from the prior one, we open a new connection
 * to the new storage and do a restore for that part.
 *
 * This permits handling multiple storage daemons for a single
 * restore.  E.g. your Full is stored on tape, and Incrementals
 * on disk.
 */
static inline bool do_ndmp_restore_bootstrap(JCR *jcr)
{
   int cnt;
   BSOCK *sd;
   BSR *bsr;
   NIS *nis = NULL;
   int32_t current_fi;
   bootstrap_info info;
   BSR_FINDEX *fileindex;
   struct ndm_session ndmp_sess;
   struct ndm_job_param ndmp_job;
   bool session_initialized = false;
   bool retval = false;
   int NdmpLoglevel;

   if (jcr->res.client->ndmp_loglevel > me->ndmp_loglevel) {
      NdmpLoglevel = jcr->res.client->ndmp_loglevel;
   } else {
      NdmpLoglevel = me->ndmp_loglevel;
   }

   /*
    * We first parse the BSR ourself so we know what to restore.
    */
   jcr->bsr = parse_bsr(jcr, jcr->RestoreBootstrap);
   if (!jcr->bsr) {
      Jmsg(jcr, M_FATAL, 0, _("Error parsing bootstrap file.\n"));
      goto bail_out;
   }

   /*
    * Setup all paired read storage.
    */
   set_paired_storage(jcr);
   if (!jcr->res.pstore) {
      Jmsg(jcr, M_FATAL, 0,
           _("Read storage %s doesn't point to storage definition with paired storage option.\n"),
           jcr->res.rstore->name());
      goto bail_out;
   }

   /*
    * Open the bootstrap file
    */
   if (!open_bootstrap_file(jcr, info)) {
      goto bail_out;
   }

   nis = (NIS *)malloc(sizeof(NIS));
   memset(nis, 0, sizeof(NIS));

   /*
    * Read the bootstrap file
    */
   bsr = jcr->bsr;
   while (!feof(info.bs)) {
      if (!select_next_rstore(jcr, info)) {
         goto cleanup;
      }

      /*
       * Initialize the ndmp restore job. We build the generic job once per storage daemon
       * and reuse the job definition for each seperate sub-restore we perform as
       * part of the whole job. We only free the env_table between every sub-restore.
       */
      if (!ndmp_build_client_job(jcr, jcr->res.client, jcr->res.pstore, NDM_JOB_OP_EXTRACT, &ndmp_job)) {
         goto cleanup;
      }

      /*
       * Open a message channel connection with the Storage
       * daemon. This is to let him know that our client
       * will be contacting him for a backup session.
       *
       */
      Dmsg0(10, "Open connection with storage daemon\n");
      jcr->setJobStatus(JS_WaitSD);

      /*
       * Start conversation with Storage daemon
       */
      if (!connect_to_storage_daemon(jcr, 10, me->SDConnectTimeout, true)) {
         goto cleanup;
      }
      sd = jcr->store_bsock;

      /*
       * Now start a job with the Storage daemon
       */
      if (!start_storage_daemon_job(jcr, jcr->rstorage, NULL)) {
         goto cleanup;
      }

      jcr->setJobStatus(JS_Running);

      /*
       * Send the bootstrap file -- what Volumes/files to restore
       */
      if (!send_bootstrap_file(jcr, sd, info) ||
          !response(jcr, sd, OKbootstrap, "Bootstrap", DISPLAY_ERROR)) {
         goto cleanup;
      }

      if (!sd->fsend("run")) {
         goto cleanup;
      }

      /*
       * Now start a Storage daemon message thread
       */
      if (!start_storage_daemon_message_thread(jcr)) {
         goto cleanup;
      }
      Dmsg0(50, "Storage daemon connection OK\n");

      /*
       * Walk each fileindex of the current BSR record. Each different fileindex is
       * a seperate NDMP stream.
       */
      cnt = 0;
      for (fileindex = bsr->FileIndex; fileindex; fileindex = fileindex->next) {
         for (current_fi = fileindex->findex; current_fi <= fileindex->findex2; current_fi++) {
            /*
             * See if this is the first Restore NDMP stream or not. For NDMP we can have multiple Backup
             * runs as part of the same Job. When we are restoring data from a Native Storage Daemon
             * we let it know to expect a next restore session. It will generate a new authorization
             * key so we wait for the nextrun_ready conditional variable to be raised by the msg_thread.
             */
            if (jcr->store_bsock && cnt > 0) {
               jcr->store_bsock->fsend("nextrun");
               P(mutex);
               pthread_cond_wait(&jcr->nextrun_ready, &mutex);
               V(mutex);
            }

            /*
             * Perform the actual NDMP job.
             * Initialize a new NDMP session
             */
            memset(&ndmp_sess, 0, sizeof(ndmp_sess));
            ndmp_sess.conn_snooping = (me->ndmp_snooping) ? 1 : 0;
            ndmp_sess.control_agent_enabled = 1;

            ndmp_sess.param = (struct ndm_session_param *)malloc(sizeof(struct ndm_session_param));
            memset(ndmp_sess.param, 0, sizeof(struct ndm_session_param));
            ndmp_sess.param->log.deliver = ndmp_loghandler;
            ndmp_sess.param->log_level = native_to_ndmp_loglevel(NdmpLoglevel, debug_level, nis);
            nis->jcr = jcr;
            ndmp_sess.param->log.ctx = nis;
            ndmp_sess.param->log_tag = bstrdup("DIR-NDMP");

            /*
             * Initialize the session structure.
             */
            if (ndma_session_initialize(&ndmp_sess)) {
               goto cleanup_ndmp;
            }
            session_initialized = true;

            /*
             * Copy the actual job to perform.
             */
            jcr->jr.FileIndex = current_fi;
            if (bsr->sessid && bsr->sesstime) {
               jcr->jr.VolSessionId = bsr->sessid->sessid;
               jcr->jr.VolSessionTime = bsr->sesstime->sesstime;
            } else {
               Jmsg(jcr, M_FATAL, 0, _("Wrong BSR missing sessid and/or sesstime\n"));
               goto cleanup_ndmp;
            }

            memcpy(&ndmp_sess.control_acb->job, &ndmp_job, sizeof(struct ndm_job_param));
            if (!fill_restore_environment(jcr,
                                          current_fi,
                                          &ndmp_sess.control_acb->job)) {
               goto cleanup_ndmp;
            }

            ndma_job_auto_adjust(&ndmp_sess.control_acb->job);
            if (!ndmp_validate_job(jcr, &ndmp_sess.control_acb->job)) {
               goto cleanup_ndmp;
            }

            /*
             * Commission the session for a run.
             */
            if (ndma_session_commission(&ndmp_sess)) {
               goto cleanup_ndmp;
            }

            /*
             * Setup the DMA.
             */
            if (ndmca_connect_control_agent(&ndmp_sess)) {
               goto cleanup_ndmp;
            }

            ndmp_sess.conn_open = 1;
            ndmp_sess.conn_authorized = 1;

            /*
             * Let the DMA perform its magic.
             */
            if (ndmca_control_agent(&ndmp_sess) != 0) {
               goto cleanup_ndmp;
            }

            /*
             * See if there were any errors during the restore.
             */
            if (!extract_post_restore_stats(jcr, &ndmp_sess)) {
               goto cleanup_ndmp;
            }

            /*
             * Reset the NDMP session states.
             */
            ndma_session_decommission(&ndmp_sess);

            /*
             * Cleanup the job after it has run.
             */
            ndma_destroy_env_list(&ndmp_sess.control_acb->job.env_tab);
            ndma_destroy_env_list(&ndmp_sess.control_acb->job.result_env_tab);
            ndma_destroy_nlist(&ndmp_sess.control_acb->job.nlist_tab);

            /*
             * Release any tape device name allocated.
             */
            if (ndmp_sess.control_acb->job.tape_device) {
               free(ndmp_sess.control_acb->job.tape_device);
               ndmp_sess.control_acb->job.tape_device = NULL;
            }

            /*
             * Destroy the session.
             */
            ndma_session_destroy(&ndmp_sess);

            /*
             * Free the param block.
             */
            free(ndmp_sess.param->log_tag);
            free(ndmp_sess.param);
            ndmp_sess.param = NULL;

            /*
             * Reset the initialized state so we don't try to cleanup again.
             */
            session_initialized = false;

            /*
             * Keep track that we finished this part of the restore.
             */
            cnt++;
         }
      }

      /*
       * Tell the storage daemon we are done.
       */
      jcr->store_bsock->fsend("finish");
      wait_for_storage_daemon_termination(jcr);
   }

   /*
    * Jump to the generic cleanup done for every Job.
    */
   retval = true;
   goto cleanup;

cleanup_ndmp:
   /*
    * Only need to cleanup when things are initialized.
    */
   if (session_initialized) {
      ndma_destroy_env_list(&ndmp_sess.control_acb->job.env_tab);
      ndma_destroy_env_list(&ndmp_sess.control_acb->job.result_env_tab);
      ndma_destroy_nlist(&ndmp_sess.control_acb->job.nlist_tab);

      if (ndmp_sess.control_acb->job.tape_device) {
         free(ndmp_sess.control_acb->job.tape_device);
      }

      /*
       * Destroy the session.
       */
      ndma_session_destroy(&ndmp_sess);
   }

   if (ndmp_sess.param) {
      free(ndmp_sess.param->log_tag);
      free(ndmp_sess.param);
   }

cleanup:
   if (nis) {
      free(nis);
   }
   free_paired_storage(jcr);
   close_bootstrap_file(info);

bail_out:
   free_tree(jcr->restore_tree_root);
   jcr->restore_tree_root = NULL;
   return retval;
}

/*
 * Run a NDMP restore session.
 */
bool do_ndmp_restore(JCR *jcr)
{
   JOB_DBR rjr;                       /* restore job record */
   int status;

   memset(&rjr, 0, sizeof(rjr));
   jcr->jr.JobLevel = L_FULL;         /* Full restore */
   if (!db_update_job_start_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
      goto bail_out;
   }
   Dmsg0(20, "Updated job start record\n");

   Dmsg1(20, "RestoreJobId=%d\n", jcr->res.job->RestoreJobId);

   /*
    * Validate the Job to have a NDMP client.
    */
   if (!ndmp_validate_client(jcr)) {
      return false;
   }

   if (!jcr->RestoreBootstrap) {
      Jmsg(jcr, M_FATAL, 0, _("Cannot restore without a bootstrap file.\n"
           "You probably ran a restore job directly. All restore jobs must\n"
           "be run using the restore command.\n"));
      goto bail_out;
   }

   /*
    * Print Job Start message
    */
   Jmsg(jcr, M_INFO, 0, _("Start Restore Job %s\n"), jcr->Job);

   /*
    * Read the bootstrap file and do the restore
    */
   if (!do_ndmp_restore_bootstrap(jcr)) {
      goto bail_out;
   }

   /*
    * Wait for Job Termination
    */
   status = ndmp_wait_for_job_termination(jcr);
   ndmp_restore_cleanup(jcr, status);
   return true;

bail_out:
   return false;
}

/*
 * Cleanup a NDMP restore session.
 */
void ndmp_restore_cleanup(JCR *jcr, int TermCode)
{
   char term_code[100];
   const char *term_msg;
   int msg_type = M_INFO;

   Dmsg0(20, "In ndmp_restore_cleanup\n");
   update_job_end(jcr, TermCode);

   if (jcr->unlink_bsr && jcr->RestoreBootstrap) {
      secure_erase(jcr, jcr->RestoreBootstrap);
      jcr->unlink_bsr = false;
   }

   if (job_canceled(jcr)) {
      cancel_storage_daemon_job(jcr);
   }

   switch (TermCode) {
   case JS_Terminated:
      if (jcr->ExpectedFiles > jcr->jr.JobFiles) {
         term_msg = _("Restore OK -- warning file count mismatch");
      } else {
         term_msg = _("Restore OK");
      }
      break;
   case JS_Warnings:
         term_msg = _("Restore OK -- with warnings");
         break;
   case JS_FatalError:
   case JS_ErrorTerminated:
      term_msg = _("*** Restore Error ***");
      msg_type = M_ERROR;          /* Generate error message */
      if (jcr->store_bsock) {
         jcr->store_bsock->signal(BNET_TERMINATE);
         if (jcr->SD_msg_chan_started) {
            pthread_cancel(jcr->SD_msg_chan);
         }
      }
      break;
   case JS_Canceled:
      term_msg = _("Restore Canceled");
      if (jcr->store_bsock) {
         jcr->store_bsock->signal(BNET_TERMINATE);
         if (jcr->SD_msg_chan_started) {
            pthread_cancel(jcr->SD_msg_chan);
         }
      }
      break;
   default:
      term_msg = term_code;
      sprintf(term_code, _("Inappropriate term code: %c\n"), TermCode);
      break;
   }

   generate_restore_summary(jcr, msg_type, term_msg);

   Dmsg0(20, "Leaving ndmp_restore_cleanup\n");
}

#else
bool do_ndmp_restore_init(JCR *jcr)
{
   Jmsg(jcr, M_FATAL, 0, _("NDMP protocol not supported\n"));
   return false;
}

bool do_ndmp_restore(JCR *jcr)
{
   Jmsg(jcr, M_FATAL, 0, _("NDMP protocol not supported\n"));
   return false;
}

void ndmp_restore_cleanup(JCR *jcr, int TermCode)
{
   Jmsg(jcr, M_FATAL, 0, _("NDMP protocol not supported\n"));
}
#endif /* HAVE_NDMP */
