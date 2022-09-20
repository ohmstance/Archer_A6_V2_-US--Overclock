/* MiniDLNA project
 *
 * http://sourceforge.net/projects/minidlna/
 *
 * MiniDLNA media server
 * Copyright (C) 2008-2012  Justin Maggard
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 *
 * Portions of the code from the MiniUPnP project:
 *
 * Copyright (c) 2006-2007, Thomas Bernard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of the author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <libgen.h>
#include <pwd.h>
#include <ucontext.h>

#include "config.h"

#ifdef ENABLE_NLS
#include <locale.h>
#include <libintl.h>
#endif

#include "upnpglobalvars.h"
#include "sql.h"
#include "upnphttp.h"
#include "upnpdescgen.h"
#include "minidlnapath.h"
#include "getifaddr.h"
#include "upnpsoap.h"
#include "options.h"
#include "utils.h"
#include "minissdp.h"
#include "minidlnatypes.h"
#include "process.h"
#include "upnpevents.h"
#include "scanner.h"
#include "inotify.h"
#include "log.h"
#include "tivo_beacon.h"
#include "tivo_utils.h"
#include "minidlna.h"

#if SQLITE_VERSION_NUMBER < 3005001
# warning "Your SQLite3 library appears to be too old!  Please use 3.5.1 or newer."
# define sqlite3_threadsafe() 0
#endif

#define MAX_EVENTS    (10)

struct signal_struct 
{
    int signum;
    char *signame;
    void (*action)(int signum);
};

extern int gnet_ubus_init();
extern void gnet_ubus_exit();

static void sigterm(int signum);
static void sighup(int signum);
static void sigusr1(int signum);
static void sigexit(int signum);
static void signal_handler(int signum, siginfo_t *siginfo, void *context);
static void register_dump(struct sigcontext *sigctx);

static struct signal_struct signals[] = 
{
    {SIGINT,  "Interrupt", sigterm}, 
    {SIGSEGV, "Segmentation violation", sigexit},
    {SIGFPE,  "Floating-point exception", sigexit},
    {SIGILL,  "Illegal instruction", sigexit},
    {SIGTERM, "Termination", sigterm}, 
    {SIGABRT, "Abort", sigexit},
    {SIGBUS,  "BUS error", sigexit},
    {SIGHUP,  "Hangup", sighup},
    {SIGUSR1, "SIGUSR1", sigusr1},
    {SIGCHLD, "Child stopped or terminated", process_handle_child_termination},
    {SIGPIPE, "Broken pipe", SIG_IGN},
    {0, NULL, NULL}
};

static int epollfd;

struct event_struct *
event_malloc(int sock)
{
    struct event_struct *evt;

    evt = calloc(1, sizeof(struct event_struct));
    if (!evt)
    {
        return NULL;
    }

    evt->sock = sock;
    return evt;
}

void 
event_free(struct event_struct *evt)
{
    if (!evt) 
    {
        return;
    }

#if 0
    if (evt->sock > -1)
    {
        close(evt->sock);
        evt->sock = -1;
    }
#endif

    free(evt);
}

int event_ctl(struct event_struct *evt, int opt, int evts)
{
    struct epoll_event epevt;

    epevt.events = evts;
    epevt.data.ptr = evt;
    return epoll_ctl(epollfd, opt, evt->sock, &epevt);
}

int event_del(struct event_struct *evt)
{
    return epoll_ctl(epollfd, EPOLL_CTL_DEL, evt->sock, NULL);
}

/* Handler for the SIGTERM signal (kill) 
 * SIGINT is also handled */
static void
sigterm(int sig)
{
	signal(sig, SIG_IGN);	/* Ignore this signal while we are quitting */

	DPRINTF(E_WARN, L_GENERAL, "received signal %d, good-bye\n", sig);

	quitting = 1;
}

static void
sigusr1(int sig)
{
	signal(sig, sigusr1);
	DPRINTF(E_WARN, L_GENERAL, "received signal %d, clear cache\n", sig);

	memset(&clients, '\0', sizeof(clients));
}

static void
sighup(int sig)
{
	signal(sig, sighup);
	DPRINTF(E_WARN, L_GENERAL, "received signal %d, re-read\n", sig);

	reload_ifaces(1);
}

static void 
sigexit(int signum)
{
    struct signal_struct *sig;

    for (sig = signals; sig->signum != 0; sig++) 
    {
        if (sig->signum == signum)
        {
            DPRINTF(E_WARN, L_GENERAL, "-------------- Exception --------------\n");
            DPRINTF(E_WARN, L_GENERAL, "Signal(%d): %s\n", signum, sig->signame);
            break;
        }
    }

    exit(0);
}

static void 
signal_handler(int signum, siginfo_t *siginfo, void *context)
{
    struct ucontext *ctx = (struct ucontext *)context;
    struct signal_struct *sig;

    DPRINTF(E_WARN, L_GENERAL, "Receive Signal(%d)\n", signum);

    /* Dump register value */
    switch (signum)
    {
    case SIGINT:
    case SIGSEGV:
    case SIGFPE:
    case SIGILL:
    case SIGTERM:
    case SIGABRT:
    case SIGBUS:
    case SIGHUP:
        register_dump((struct sigcontext *)&ctx->uc_mcontext);
        break;

    default:
        break;
    }

    for (sig = signals; sig->signum != 0; sig++) 
    {
        if (sig->signum == signum)
        {
            sig->action(signum);
        }
    }
}

#if defined(__ARM__)

static void
register_dump(struct sigcontext *sigctx)
{
    static char *arm_reg_names[] =
    {
        "r0", "r1", "r2", "r3",
        "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "fp",
        "ip", "sp", "lr", "pc",
        "cpsr"
    };
    unsigned long *ptr = &sigctx->arm_r0;
    char buf[512];
    char *str = buf;
    int size;
    int index;
    int i;

    size = 512;
    index = snprintf(str, size, "\n-------------- Register --------------\n");
    for (i = 0; i < 17; i++)
    {
        if (i > 0 && (i % 2) == 0)
        {
            index += snprintf(str + index, size - index, "\n");
        }
        index += snprintf(str + index, size - index, "%-4s: %08lx        ", 
                          arm_reg_names[i], *ptr);
        ptr++;
    }

    DPRINTF(E_WARN, L_GENERAL, "%s\n", buf);
}

#else

static void
register_dump(struct sigcontext *sigctx)
{
    /* ignore */
}
#endif

/* record the startup time */
static void
set_startup_time(void)
{
	startup_time = time(NULL);
}

static void
getfriendlyname(char *buf, int len)
{
	char *p = NULL;
	char hn[256];
	int off;

	if (gethostname(hn, sizeof(hn)) == 0)
	{
		strncpyt(buf, hn, len);
		p = strchr(buf, '.');
		if (p)
			*p = '\0';
	}
	else
		strcpy(buf, "Unknown");

	off = strlen(buf);
	off += snprintf(buf+off, len-off, ": ");
#ifdef READYNAS
	FILE *info;
	char ibuf[64], *key, *val;
	snprintf(buf+off, len-off, "ReadyNAS");
	info = fopen("/proc/sys/dev/boot/info", "r");
	if (!info)
		return;
	while ((val = fgets(ibuf, 64, info)) != NULL)
	{
		key = strsep(&val, ": \t");
		val = trim(val);
		if (strcmp(key, "model") == 0)
		{
			snprintf(buf+off, len-off, "%s", val);
			key = strchr(val, ' ');
			if (key)
			{
				strncpyt(modelnumber, key+1, MODELNUMBER_MAX_LEN);
				*key = '\0';
			}
			snprintf(modelname, MODELNAME_MAX_LEN,
				"Windows Media Connect compatible (%s)", val);
		}
		else if (strcmp(key, "serial") == 0)
		{
			strncpyt(serialnumber, val, SERIALNUMBER_MAX_LEN);
			if (serialnumber[0] == '\0')
			{
				char mac_str[13];
				if (getsyshwaddr(mac_str, sizeof(mac_str)) == 0)
					strcpy(serialnumber, mac_str);
				else
					strcpy(serialnumber, "0");
			}
			break;
		}
	}
	fclose(info);
#if PNPX
	memcpy(pnpx_hwid+4, "01F2", 4);
	if (strcmp(modelnumber, "NVX") == 0)
		memcpy(pnpx_hwid+17, "0101", 4);
	else if (strcmp(modelnumber, "Pro") == 0 ||
	         strcmp(modelnumber, "Pro 6") == 0 ||
	         strncmp(modelnumber, "Ultra 6", 7) == 0)
		memcpy(pnpx_hwid+17, "0102", 4);
	else if (strcmp(modelnumber, "Pro 2") == 0 ||
	         strncmp(modelnumber, "Ultra 2", 7) == 0)
		memcpy(pnpx_hwid+17, "0103", 4);
	else if (strcmp(modelnumber, "Pro 4") == 0 ||
	         strncmp(modelnumber, "Ultra 4", 7) == 0)
		memcpy(pnpx_hwid+17, "0104", 4);
	else if (strcmp(modelnumber+1, "100") == 0)
		memcpy(pnpx_hwid+17, "0105", 4);
	else if (strcmp(modelnumber+1, "200") == 0)
		memcpy(pnpx_hwid+17, "0106", 4);
	/* 0107 = Stora */
	else if (strcmp(modelnumber, "Duo v2") == 0)
		memcpy(pnpx_hwid+17, "0108", 4);
	else if (strcmp(modelnumber, "NV+ v2") == 0)
		memcpy(pnpx_hwid+17, "0109", 4);
#endif
#else
	char * logname;
	logname = getenv("LOGNAME");
#ifndef STATIC // Disable for static linking
	if (!logname)
	{
		struct passwd * pwent;
		pwent = getpwuid(getuid());
		if (pwent)
			logname = pwent->pw_name;
	}
#endif
	snprintf(buf+off, len-off, "%s", logname?logname:"Unknown");
#endif
}

static int
open_db(sqlite3 **sq3)
{
	char path[PATH_MAX];
	int new_db = 0;

	snprintf(path, sizeof(path), "%s/files.db", db_path);
	if (access(path, F_OK) != 0)
	{
		new_db = 1;
		make_dir(db_path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
	}
	if (sqlite3_open(path, &db) != SQLITE_OK)
		DPRINTF(E_FATAL, L_GENERAL, "ERROR: Failed to open sqlite database!  Exiting...\n");
	if (sq3)
		*sq3 = db;
	sqlite3_busy_timeout(db, 5000);
    sqlite3_soft_heap_limit64(1 * 1024 * 1024);
	sql_exec(db, "pragma page_size = 4096");
	sql_exec(db, "pragma journal_mode = OFF");
	sql_exec(db, "pragma synchronous = OFF;");
	sql_exec(db, "pragma default_cache_size = 8192;");
    // sql_exec(db, "pragma cache_size = 8192");
    // sql_exec(db, "pragma auto_vacuum = 0;");
    // sql_exec(db, "pragma temp_store = FILE;");

	return new_db;
}

static void
check_db(sqlite3 *db, int new_db, pid_t *scanner_pid)
{
	struct media_dir_s *media_path = NULL;
	char cmd[PATH_MAX*2];
	char **result;
	int i, rows = 0;
	int ret;

	if (!new_db)
	{
		/* Check if any new media dirs appeared */
		media_path = media_dirs;
		while (media_path)
		{
			ret = sql_get_int_field(db, "SELECT TIMESTAMP from DETAILS where PATH = %Q", media_path->path);
			if (ret != media_path->types)
			{
				ret = 1;
				goto rescan;
			}
			media_path = media_path->next;
		}
		/* Check if any media dirs disappeared */
		sql_get_table(db, "SELECT VALUE from SETTINGS where KEY = 'media_dir'", &result, &rows, NULL);
		for (i=1; i <= rows; i++)
		{
			media_path = media_dirs;
			while (media_path)
			{
				if (strcmp(result[i], media_path->path) == 0)
					break;
				media_path = media_path->next;
			}
			if (!media_path)
			{
				ret = 2;
				sqlite3_free_table(result);
				goto rescan;
			}
		}
		sqlite3_free_table(result);
	}

	ret = db_upgrade(db);
	if (ret != 0)
	{
rescan:
		if (ret < 0)
			DPRINTF(E_WARN, L_GENERAL, "Creating new database at %s/files.db\n", db_path);
		else if (ret == 1)
			DPRINTF(E_WARN, L_GENERAL, "New media_dir detected; rescanning...\n");
		else if (ret == 2)
			DPRINTF(E_WARN, L_GENERAL, "Removed media_dir detected; rescanning...\n");
		else
			DPRINTF(E_WARN, L_GENERAL, "Database version mismatch; need to recreate...\n");
		sqlite3_close(db);

		snprintf(cmd, sizeof(cmd), "rm -rf %s/files.db %s/art_cache", db_path, db_path);
		if (system(cmd) != 0)
			DPRINTF(E_FATAL, L_GENERAL, "Failed to clean old file cache!  Exiting...\n");

		open_db(&db);
		if (CreateDatabase() != 0)
			DPRINTF(E_FATAL, L_GENERAL, "ERROR: Failed to create sqlite database!  Exiting...\n");
#if USE_FORK
		scanning = 1;
		sqlite3_close(db);
		*scanner_pid = process_fork();
		open_db(&db);
		if (*scanner_pid == 0) /* child (scanner) process */
		{
			start_scanner();
			sqlite3_close(db);
			log_close();
			freeoptions();
			exit(EXIT_SUCCESS);
		}
		else if (*scanner_pid < 0)
		{
			start_scanner();
		}
#else
		start_scanner();
#endif
	}
}

static int
writepidfile(const char *fname, int pid, uid_t uid)
{
	FILE *pidfile;
	struct stat st;
	char path[PATH_MAX], *dir;
	int ret = 0;

	if(!fname || *fname == '\0')
		return -1;

	/* Create parent directory if it doesn't already exist */
	strncpyt(path, fname, sizeof(path));
	dir = dirname(path);
	if (stat(dir, &st) == 0)
	{
		if (!S_ISDIR(st.st_mode))
		{
			DPRINTF(E_ERROR, L_GENERAL, "Pidfile path is not a directory: %s\n",
				fname);
			return -1;
		}
	}
	else
	{
		if (make_dir(dir, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0)
		{
			DPRINTF(E_ERROR, L_GENERAL, "Unable to create pidfile directory: %s\n",
				fname);
			return -1;
		}
		if (uid >= 0)
		{
			if (chown(dir, uid, -1) != 0)
				DPRINTF(E_WARN, L_GENERAL, "Unable to change pidfile ownership: %s\n",
					dir, strerror(errno));
		}
	}
	
	pidfile = fopen(fname, "w");
	if (!pidfile)
	{
		DPRINTF(E_ERROR, L_GENERAL, "Unable to open pidfile for writing %s: %s\n",
			fname, strerror(errno));
		return -1;
	}

	if (fprintf(pidfile, "%d\n", pid) <= 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, 
			"Unable to write to pidfile %s: %s\n", fname);
		ret = -1;
	}
	if (uid >= 0)
	{
		if (fchown(fileno(pidfile), uid, -1) != 0)
			DPRINTF(E_WARN, L_GENERAL, "Unable to change pidfile ownership: %s\n",
				pidfile, strerror(errno));
	}

	fclose(pidfile);

	return ret;
}

static int rlimit_init()
{
    struct rlimit rlim;
    int ret;

#if 0
    ret = getrlimit(RLIMIT_NOFILE, &rlim);
    if (ret != 0)
    {
        return -1;
    }

    rlim.rlim_cur += 1024;
    rlim.rlim_max += 1024;
#else
    rlim.rlim_cur = RLIM_INFINITY;
    rlim.rlim_max = RLIM_INFINITY;
#endif
    DPRINTF(E_DEBUG, L_GENERAL, "NOFILE: cur = %lu, max = %lu\n", rlim.rlim_cur, rlim.rlim_max);
    ret = setrlimit(RLIMIT_NOFILE, &rlim);

    return ret;
}

/* init phase :
 * 1) read configuration file
 * 2) read command line arguments
 * 3) daemonize
 * 4) check and write pid file
 * 5) set startup time stamp
 * 6) compute presentation URL
 * 7) set signal handlers */
static int
init(int argc, char **argv)
{
	int i;
	int pid;
	int debug_flag = 0;
	int verbose_flag = 0;
	int options_flag = 0;
	struct sigaction sa;
	const char * presurl = NULL;
	const char * optionsfile = "/etc/minidlna.conf";
	char mac_str[13];
	char *string, *word;
	char *path;
	char buf[PATH_MAX];
	char log_str[75] = "general,artwork,database,inotify,scanner,metadata,http,ssdp,tivo=warn";
	char *log_level = NULL;
	struct media_dir_s *media_dir;
	int ifaces = 0;
	media_types types;
	uid_t uid = -1;
    int count;
    int genable;
    struct signal_struct *sig;

	/* first check if "-f" option is used */
	for (i=2; i<argc; i++)
	{
		if (strcmp(argv[i-1], "-f") == 0)
		{
			optionsfile = argv[i];
			options_flag = 1;
			break;
		}
	}

	/* set up uuid based on mac address */
	if (getsyshwaddr(mac_str, sizeof(mac_str)) < 0)
	{
		DPRINTF(E_OFF, L_GENERAL, "No MAC address found.  Falling back to generic UUID.\n");
		strcpy(mac_str, "554e4b4e4f57");
	}
	strcpy(uuidvalue+5, "4d696e69-444c-164e-9d41-");
	strncat(uuidvalue, mac_str, 12);

	getfriendlyname(friendly_name, FRIENDLYNAME_MAX_LEN);
	
	runtime_vars.port = 8200;
	runtime_vars.notify_interval = 895;	/* seconds between SSDP announces */
	runtime_vars.max_connections = 50;
	runtime_vars.root_container = NULL;
	runtime_vars.ifaces[0] = NULL;

	/* read options file first since
	 * command line arguments have final say */
	if (readoptionsfile(optionsfile) < 0)
	{
		/* only error if file exists or using -f */
		if(access(optionsfile, F_OK) == 0 || options_flag)
			DPRINTF(E_FATAL, L_GENERAL, "Error reading configuration file %s\n", optionsfile);
	}

	for (i=0; i<num_options; i++)
	{
		switch (ary_options[i].id)
		{
		case UPNPIFNAME:
			for (string = ary_options[i].value; (word = strtok(string, ",")); string = NULL)
			{
				if (ifaces >= MAX_LAN_ADDR)
				{
					DPRINTF(E_ERROR, L_GENERAL, "Too many interfaces (max: %d), ignoring %s\n",
						MAX_LAN_ADDR, word);
					break;
				}
				runtime_vars.ifaces[ifaces++] = word;
			}
			break;
		case UPNPPORT:
			runtime_vars.port = atoi(ary_options[i].value);
			break;
		case UPNPPRESENTATIONURL:
			presurl = ary_options[i].value;
			break;
		case UPNPNOTIFY_INTERVAL:
			runtime_vars.notify_interval = atoi(ary_options[i].value);
			break;
		case UPNPSERIAL:
			strncpyt(serialnumber, ary_options[i].value, SERIALNUMBER_MAX_LEN);
			break;				
		case UPNPMODEL_NAME:
			strncpyt(modelname, ary_options[i].value, MODELNAME_MAX_LEN);
			break;
		case UPNPMODEL_NUMBER:
			strncpyt(modelnumber, ary_options[i].value, MODELNUMBER_MAX_LEN);
			break;
		case UPNPFRIENDLYNAME:
			strncpyt(friendly_name, ary_options[i].value, FRIENDLYNAME_MAX_LEN);
			break;
		case UPNPMEDIADIR:
			types = NO_MEDIA;
            genable = 0;
			path = ary_options[i].value;
			word = strchr(path, ',');
			if (word && (access(path, F_OK) != 0))
			{
				/* types = 0; */
                count = 0;
				while (*path)
				{
					if (*path == ',')
					{
                        count++;
                        if (count >= 2)
                        {
                            path++;
                            break;
                        }
					}
					else if (*path == 'A' || *path == 'a')
						types |= TYPE_AUDIO;
					else if (*path == 'V' || *path == 'v')
						types |= TYPE_VIDEO;
					else if (*path == 'P' || *path == 'p')
						types |= TYPE_IMAGES;
                    else if (*path == 'G' || *path == 'g')
                        genable = 1;
                    else if (*path == '/')
                        break;
					else
						DPRINTF(E_FATAL, L_GENERAL, "Media directory entry not understood [%s]\n",
							ary_options[i].value);
					path++;
				}
			}
            if (types == NO_MEDIA)
            {
                types = ALL_MEDIA;
            }
			path = realpath(path, buf);
			if (!path || access(path, F_OK) != 0)
			{
				DPRINTF(E_ERROR, L_GENERAL, "Media directory \"%s\" not accessible [%s]\n",
					ary_options[i].value, strerror(errno));
				break;
			}
			media_dir = calloc(1, sizeof(struct media_dir_s));
			media_dir->path = strdup(path);
			media_dir->types = types;
            media_dir->genable = genable;
			if (media_dirs)
			{
				struct media_dir_s *all_dirs = media_dirs;
				while( all_dirs->next )
					all_dirs = all_dirs->next;
				all_dirs->next = media_dir;
			}
			else
				media_dirs = media_dir;
			break;
		case UPNPALBUMART_NAMES:
			for (string = ary_options[i].value; (word = strtok(string, "/")); string = NULL)
			{
				struct album_art_name_s * this_name = calloc(1, sizeof(struct album_art_name_s));
				int len = strlen(word);
				if (word[len-1] == '*')
				{
					word[len-1] = '\0';
					this_name->wildcard = 1;
				}
				this_name->name = strdup(word);
				if (album_art_names)
				{
					struct album_art_name_s * all_names = album_art_names;
					while( all_names->next )
						all_names = all_names->next;
					all_names->next = this_name;
				}
				else
					album_art_names = this_name;
			}
			break;
		case UPNPDBDIR:
			path = realpath(ary_options[i].value, buf);
			if (!path)
				path = (ary_options[i].value);
			make_dir(path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
			if (access(path, F_OK) != 0)
				DPRINTF(E_FATAL, L_GENERAL, "Database path not accessible! [%s]\n", path);
			strncpyt(db_path, path, PATH_MAX);
			break;
		case UPNPLOGDIR:
			path = realpath(ary_options[i].value, buf);
			if (!path)
				path = (ary_options[i].value);
			make_dir(path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
			if (access(path, F_OK) != 0)
				DPRINTF(E_FATAL, L_GENERAL, "Log path not accessible! [%s]\n", path);
			strncpyt(log_path, path, PATH_MAX);
			break;
		case UPNPLOGLEVEL:
			log_level = ary_options[i].value;
			break;
		case UPNPINOTIFY:
			if ((strcmp(ary_options[i].value, "yes") != 0) && !atoi(ary_options[i].value))
				CLEARFLAG(INOTIFY_MASK);
			break;
		case ENABLE_TIVO:
			if ((strcmp(ary_options[i].value, "yes") == 0) || atoi(ary_options[i].value))
				SETFLAG(TIVO_MASK);
			break;
		case ENABLE_DLNA_STRICT:
			if ((strcmp(ary_options[i].value, "yes") == 0) || atoi(ary_options[i].value))
				SETFLAG(DLNA_STRICT_MASK);
			break;
		case ROOT_CONTAINER:
			switch (ary_options[i].value[0]) {
			case '.':
				runtime_vars.root_container = NULL;
				break;
			case 'B':
			case 'b':
				runtime_vars.root_container = BROWSEDIR_ID;
				break;
			case 'M':
			case 'm':
				runtime_vars.root_container = MUSIC_ID;
				break;
			case 'V':
			case 'v':
				runtime_vars.root_container = VIDEO_ID;
				break;
			case 'P':
			case 'p':
				runtime_vars.root_container = IMAGE_ID;
				break;
			default:
				DPRINTF(E_ERROR, L_GENERAL, "Invalid root container! [%s]\n",
					ary_options[i].value);
				break;
			}
			break;
		case UPNPMINISSDPDSOCKET:
			minissdpdsocketpath = ary_options[i].value;
			break;
		case UPNPUUID:
			strcpy(uuidvalue+5, ary_options[i].value);
			break;
		case USER_ACCOUNT:
			uid = strtol(ary_options[i].value, &string, 0);
			if (*string)
			{
				/* Symbolic username given, not UID. */
				struct passwd *entry = getpwnam(ary_options[i].value);
				if (!entry)
					DPRINTF(E_FATAL, L_GENERAL, "Bad user '%s'.\n", argv[i]);
				uid = entry->pw_uid;
			}
			break;
		case FORCE_SORT_CRITERIA:
			force_sort_criteria = ary_options[i].value;
			break;
		case MAX_CONNECTIONS:
			runtime_vars.max_connections = atoi(ary_options[i].value);
			break;
		default:
			DPRINTF(E_ERROR, L_GENERAL, "Unknown option in file %s\n",
				optionsfile);
		}
	}
	if (log_path[0] == '\0')
	{
		if (db_path[0] == '\0')
			strncpyt(log_path, DEFAULT_LOG_PATH, PATH_MAX);
		else
			strncpyt(log_path, db_path, PATH_MAX);
	}
	if (db_path[0] == '\0')
		strncpyt(db_path, DEFAULT_DB_PATH, PATH_MAX);

	/* command line arguments processing */
	for (i=1; i<argc; i++)
	{
		if (argv[i][0] != '-')
		{
			DPRINTF(E_FATAL, L_GENERAL, "Unknown option: %s\n", argv[i]);
		}
		else if (strcmp(argv[i], "--help") == 0)
		{
			runtime_vars.port = -1;
			break;
		}
		else switch(argv[i][1])
		{
		case 't':
			if (i+1 < argc)
				runtime_vars.notify_interval = atoi(argv[++i]);
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 's':
			if (i+1 < argc)
				strncpyt(serialnumber, argv[++i], SERIALNUMBER_MAX_LEN);
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'm':
			if (i+1 < argc)
				strncpyt(modelnumber, argv[++i], MODELNUMBER_MAX_LEN);
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'p':
			if (i+1 < argc)
				runtime_vars.port = atoi(argv[++i]);
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'P':
			if (i+1 < argc)
			{
				if (argv[++i][0] != '/')
					DPRINTF(E_FATAL, L_GENERAL, "Option -%c requires an absolute filename.\n", argv[i-1][1]);
				else
					pidfilename = argv[i];
			}
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'd':
			debug_flag = 1;
		case 'v':
			verbose_flag = 1;
			break;
		case 'L':
			SETFLAG(NO_PLAYLIST_MASK);
			break;
		case 'w':
			if (i+1 < argc)
				presurl = argv[++i];
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'i':
			if (i+1 < argc)
			{
				i++;
				if (ifaces >= MAX_LAN_ADDR)
				{
					DPRINTF(E_ERROR, L_GENERAL, "Too many interfaces (max: %d), ignoring %s\n",
						MAX_LAN_ADDR, argv[i]);
					break;
				}
				runtime_vars.ifaces[ifaces++] = argv[i];
			}
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
		case 'f':
			i++;	/* discarding, the config file is already read */
			break;
		case 'h':
			runtime_vars.port = -1; // triggers help display
			break;
		case 'R':
			snprintf(buf, sizeof(buf), "rm -rf %s/files.db %s/art_cache", db_path, db_path);
			if (system(buf) != 0)
				DPRINTF(E_FATAL, L_GENERAL, "Failed to clean old file cache. EXITING\n");
			break;
		case 'u':
			if (i+1 != argc)
			{
				i++;
				uid = strtol(argv[i], &string, 0);
				if (*string)
				{
					/* Symbolic username given, not UID. */
					struct passwd *entry = getpwnam(argv[i]);
					if (!entry)
						DPRINTF(E_FATAL, L_GENERAL, "Bad user '%s'.\n", argv[i]);
					uid = entry->pw_uid;
				}
			}
			else
				DPRINTF(E_FATAL, L_GENERAL, "Option -%c takes one argument.\n", argv[i][1]);
			break;
			break;
#ifdef __linux__
		case 'S':
			SETFLAG(SYSTEMD_MASK);
			break;
#endif
		case 'V':
			printf("Version " MINIDLNA_VERSION "\n");
			exit(0);
			break;
		default:
			DPRINTF(E_ERROR, L_GENERAL, "Unknown option: %s\n", argv[i]);
			runtime_vars.port = -1; // triggers help display
		}
	}

	if (runtime_vars.port <= 0)
	{
		printf("Usage:\n\t"
			"%s [-d] [-v] [-f config_file] [-p port]\n"
			"\t\t[-i network_interface] [-u uid_to_run_as]\n"
			"\t\t[-t notify_interval] [-P pid_filename]\n"
			"\t\t[-s serial] [-m model_number]\n"
#ifdef __linux__
			"\t\t[-w url] [-R] [-L] [-S] [-V] [-h]\n"
#else
			"\t\t[-w url] [-R] [-L] [-V] [-h]\n"
#endif
			"\nNotes:\n\tNotify interval is in seconds. Default is 895 seconds.\n"
			"\tDefault pid file is %s.\n"
			"\tWith -d minidlna will run in debug mode (not daemonize).\n"
			"\t-w sets the presentation url. Default is http address on port 80\n"
			"\t-v enables verbose output\n"
			"\t-h displays this text\n"
			"\t-R forces a full rescan\n"
			"\t-L do not create playlists\n"
#ifdef __linux__
			"\t-S changes behaviour for systemd\n"
#endif
			"\t-V print the version number\n",
			argv[0], pidfilename);
		return 1;
	}

	if (verbose_flag)
	{
		strcpy(log_str+65, "debug");
		log_level = log_str;
	}
	else if (!log_level)
		log_level = log_str;

	/* Set the default log file path to NULL (stdout) */
	path = NULL;
	if (debug_flag)
	{
		pid = getpid();
		strcpy(log_str+65, "maxdebug");
		log_level = log_str;
	}
	else if (GETFLAG(SYSTEMD_MASK))
	{
		pid = getpid();
	}
	else
	{
		pid = process_daemonize();
		#ifdef READYNAS
		unlink("/ramfs/.upnp-av_scan");
		path = "/var/log/upnp-av.log";
		#else
		if (access(db_path, F_OK) != 0)
			make_dir(db_path, S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO);
		snprintf(buf, sizeof(buf), "%s/minidlna.log", log_path);
		path = buf;
		#endif
	}
	log_init(path, log_level);

	if (process_check_if_running(pidfilename) < 0)
	{
		DPRINTF(E_ERROR, L_GENERAL, "MiniDLNA is already running. EXITING.\n");
		return 1;
	}	

	set_startup_time();

	/* presentation url */
	if (presurl)
		strncpyt(presentationurl, presurl, PRESENTATIONURL_MAX_LEN);
	else
		strcpy(presentationurl, "/");

	/* set signal handlers */
	memset(&sa, 0, sizeof(struct sigaction));
    for (sig = signals; sig->signum != 0; sig++)
    {
        if (sig->action == SIG_IGN)
        {
            (void)signal(sig->signum, SIG_IGN);
            continue;
        }

        memset(&sa, 0, sizeof(struct sigaction));
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        sa.sa_sigaction = signal_handler;
        if (sigaction(sig->signum, &sa, NULL) == -1)
        {
            DPRINTF(E_FATAL, L_GENERAL, "Failed to set %d handler. EXITING.\n", sig->signum);
        }
    }

#if 0
	sa.sa_handler = sigterm;
	if (sigaction(SIGTERM, &sa, NULL))
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set %s handler. EXITING.\n", "SIGTERM");
	if (sigaction(SIGINT, &sa, NULL))
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set %s handler. EXITING.\n", "SIGINT");
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set %s handler. EXITING.\n", "SIGPIPE");
	if (signal(SIGHUP, &sighup) == SIG_ERR)
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set %s handler. EXITING.\n", "SIGHUP");
	signal(SIGUSR1, &sigusr1);
	sa.sa_handler = process_handle_child_termination;
	if (sigaction(SIGCHLD, &sa, NULL))
		DPRINTF(E_FATAL, L_GENERAL, "Failed to set %s handler. EXITING.\n", "SIGCHLD");
#endif

	if (writepidfile(pidfilename, pid, uid) != 0)
		pidfilename = NULL;

	if (uid >= 0)
	{
		struct stat st;
		if (stat(db_path, &st) == 0 && st.st_uid != uid && chown(db_path, uid, -1) != 0)
			DPRINTF(E_ERROR, L_GENERAL, "Unable to set db_path [%s] ownership to %d: %s\n",
				db_path, uid, strerror(errno));
	}

	if (uid != -1 && setuid(uid) == -1)
		DPRINTF(E_FATAL, L_GENERAL, "Failed to switch to uid '%d'. [%s] EXITING.\n",
			uid, strerror(errno));

	return 0;
}

/* === main === */
/* process HTTP or SSDP requests */
int
main(int argc, char **argv)
{
	int ret, i;
    struct timeval timeofday;
    time_t timeout;
    time_t lastnotifytime;
	time_t lastupdatetime = 0;
	int last_changecnt = 0;
	pid_t scanner_pid = 0;
	pthread_t inotify_thread = 0;
#ifdef TIVO_SUPPORT
	uint8_t beacon_interval = 5;
	struct sockaddr_in tivo_bcast;
	struct timeval lastbeacontime = {0, 0};
#endif
    int size = 10;
    int n, nfds;
    struct epoll_event events[MAX_EVENTS];
    struct event_struct *evt;

	for (i = 0; i < L_MAX; i++)
		log_level[i] = E_WARN;
#ifdef ENABLE_NLS
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "en_US.utf8");
	DPRINTF(E_DEBUG, L_GENERAL, "Using locale dir %s\n", bindtextdomain("minidlna", getenv("TEXTDOMAINDIR")));
	textdomain("minidlna");
#endif

	ret = init(argc, argv);
	if (ret != 0)
		return 1;

    (void)rlimit_init();

    ret = gnet_ubus_init();
    if (ret != 0)
    {
        DPRINTF(E_ERROR, L_GENERAL, "Connect to UBus failed\n");
        return 1;
    }

	DPRINTF(E_WARN, L_GENERAL, "Starting " SERVER_NAME " version " MINIDLNA_VERSION ".\n");
	if (sqlite3_libversion_number() < 3005001)
	{
		DPRINTF(E_WARN, L_GENERAL, "SQLite library is old.  Please use version 3.5.1 or newer.\n");
	}

	ret = open_db(NULL);
	if (ret == 0)
	{
		updateID = sql_get_int_field(db, "SELECT VALUE from SETTINGS where KEY = 'UPDATE_ID'");
		if (updateID == -1)
			ret = -1;
	}
	check_db(db, ret, &scanner_pid);

#ifdef HAVE_INOTIFY
	if( GETFLAG(INOTIFY_MASK) )
	{
		if (!sqlite3_threadsafe() || sqlite3_libversion_number() < 3005001)
			DPRINTF(E_ERROR, L_GENERAL, "SQLite library is not threadsafe!  "
			                            "Inotify will be disabled.\n");
		else if (pthread_create(&inotify_thread, NULL, start_inotify, NULL) != 0)
			DPRINTF(E_FATAL, L_GENERAL, "ERROR: pthread_create() failed for start_inotify. EXITING\n");
	}
#endif

    /* size argument is ignored */
    epollfd = epoll_create(size);
    if (epollfd == -1)
    {
        DPRINTF(E_FATAL, L_GENERAL, "Failed to open socket for epoll. EXITING\n");
    }

    ret = monitor_init();
    if (ret != 0)
    {
        DPRINTF(E_FATAL, L_GENERAL, "Failed to init monitor. EXITING\n");
    }

    ret = ssdp_init(runtime_vars.port);
    if (ret != 0)
    {
        DPRINTF(E_FATAL, L_GENERAL, "Failed to init ssdp. EXITING\n");
    }

    ret = upnphttp_init(runtime_vars.port);
    if (ret != 0)
    {
        DPRINTF(E_FATAL, L_GENERAL, "Failted to init upnphttp. EXITING\n");
    }

#ifdef TIVO_SUPPORT
	if (GETFLAG(TIVO_MASK))
	{
        ret = tivo_init();
        if (ret != 0)
        {
            DPRINTF(E_FATAL, L_GENERAL, "Failed to init tivo. EXITING\n");
        }

		tivo_bcast.sin_family = AF_INET;
		tivo_bcast.sin_addr.s_addr = htonl(getBcastAddress());
		tivo_bcast.sin_port = htons(2190);
	}
#endif

	reload_ifaces(0);
    lastnotifytime = time(NULL) + runtime_vars.notify_interval;

	/* main loop */
	while (!quitting)
	{
		/* Check if we need to send SSDP NOTIFY messages and do it if
		 * needed */
		if (gettimeofday(&timeofday, 0) < 0)
		{
			DPRINTF(E_ERROR, L_GENERAL, "gettimeofday(): %s\n", strerror(errno));
			timeout = runtime_vars.notify_interval;
		}
		else
		{
			/* the comparison is not very precise but who cares ? */
			if (timeofday.tv_sec >= (lastnotifytime + runtime_vars.notify_interval))
			{
				DPRINTF(E_DEBUG, L_SSDP, "Sending SSDP notifies\n");
				for (i = 0; i < n_lan_addr; i++)
				{
					SendSSDPNotifies(lan_addr[i].snotify, lan_addr[i].str,
						runtime_vars.port, runtime_vars.notify_interval);
				}
                lastnotifytime = timeofday.tv_sec;
				timeout = runtime_vars.notify_interval;
			}
			else
			{
				timeout = lastnotifytime + runtime_vars.notify_interval - timeofday.tv_sec;
			}
#ifdef TIVO_SUPPORT
			if (GETFLAG(TIVO_MASK))
			{
				if (timeofday.tv_sec >= (lastbeacontime.tv_sec + beacon_interval))
				{
					sendBeaconMessage(&tivo_bcast, sizeof(struct sockaddr_in), 1);
					memcpy(&lastbeacontime, &timeofday, sizeof(struct timeval));
					if (timeout > beacon_interval)
					{
						timeout = beacon_interval;
					}
					/* Beacons should be sent every 5 seconds or so for the first minute,
					 * then every minute or so thereafter. */
					if (beacon_interval == 5 && (timeofday.tv_sec - startup_time) > 60)
                    {
						beacon_interval = 60;
                    }
				}
				else if (timeout > (lastbeacontime.tv_sec + beacon_interval + 1 - timeofday.tv_sec))
                {
					timeout = lastbeacontime.tv_sec + beacon_interval - timeofday.tv_sec;
                }
			}
#endif
		}

		if (scanning)
		{
			if (!scanner_pid || kill(scanner_pid, 0) != 0)
			{
				scanning = 0;
				updateID++;
			}
		}

		/* epoll_wait open sockets (SSDP, HTTP listen, and all HTTP soap sockets) */
        DPRINTF(E_DEBUG, L_GENERAL, "epoll_wait start\n");
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, timeout);
        if (nfds < 0)
        {
			if(quitting) 
            {
                goto shutdown;
            }
			if(errno == EINTR) 
            {
                continue;
            }
			DPRINTF(E_ERROR, L_GENERAL, "select(all): %s\n", strerror(errno));
			DPRINTF(E_FATAL, L_GENERAL, "Failed to select open sockets. EXITING\n");
        }

#if 0
        if (nfds == 0)
        {
            DPRINTF(E_WARN, L_GENERAL, "epoll_wait return %d\n", nfds);
        }
#endif

        for (n = 0; n < nfds; n++)
        {
            evt = (struct event_struct *)events[n].data.ptr;
            if (events[n].events & EPOLLIN)
            {
                /* read handler */
                if (evt->read.handler)
                {
                    DPRINTF(E_DEBUG, L_GENERAL, "read start: %p %p\n", evt->read.handler, evt->read.data);
                    evt->read.handler(evt->read.data);
                    DPRINTF(E_DEBUG, L_GENERAL, "read end\n");
                }
            }
            if (events[n].events & EPOLLOUT)
            {
                /* write handler */
                if (evt->write.handler)
                {
                    DPRINTF(E_DEBUG, L_GENERAL, "write start\n");
                    evt->write.handler(evt->write.data);
                    DPRINTF(E_DEBUG, L_GENERAL, "write end\n");
                }
            }
        }

        /* active HTTP connections count */
        i = upnphttp_connCount();

		/* for debug */
		if (i > 1)
        {
			DPRINTF(E_DEBUG, L_GENERAL, "%d active incoming HTTP connections\n", i);
        }

		/* increment SystemUpdateID if the content database has changed,
		 * and if there is an active HTTP connection, at most once every 2 seconds */
		if (i && (timeofday.tv_sec >= (lastupdatetime + 2)))
		{
			if (scanning || sqlite3_total_changes(db) != last_changecnt)
			{
				updateID++;
				last_changecnt = sqlite3_total_changes(db);
				upnp_event_var_change_notify(EContentDirectory);
				lastupdatetime = timeofday.tv_sec;
			}
		}
	}

shutdown:
	/* kill the scanner */
	if (scanning && scanner_pid)
		kill(scanner_pid, 9);

    upnphttp_exit();
    ssdp_exit();

#ifdef TIVO_SUPPORT
    tivo_exit();
#endif
	
	for (i = 0; i < n_lan_addr; i++)
	{
		SendSSDPGoodbyes(lan_addr[i].snotify);
		close(lan_addr[i].snotify);
	}

	if (inotify_thread)
		pthread_join(inotify_thread, NULL);

	sql_exec(db, "UPDATE SETTINGS set VALUE = '%u' where KEY = 'UPDATE_ID'", updateID);
	sqlite3_close(db);

	upnpevents_removeSubscribers();

	if (pidfilename && unlink(pidfilename) < 0)
		DPRINTF(E_ERROR, L_GENERAL, "Failed to remove pidfile %s: %s\n", pidfilename, strerror(errno));

	log_close();
	freeoptions();
    gnet_ubus_exit();

	exit(EXIT_SUCCESS);
}
