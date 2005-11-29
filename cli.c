/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Standard Command Line Interface
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <unistd.h>
#include <stdlib.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/module.h>
#include <asterisk/channel.h>
#include <sys/signal.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
/* For rl_filename_completion */
#include <readline/readline.h>
/* For module directory */
#include "asterisk.h"

void ast_cli(int fd, char *fmt, ...)
{
	char stuff[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(stuff, sizeof(stuff), fmt, ap);
	va_end(ap);
	write(fd, stuff, strlen(stuff));
}

pthread_mutex_t clilock = PTHREAD_MUTEX_INITIALIZER;


struct ast_cli_entry *helpers = NULL;

static char load_help[] = 
"Usage: load <module name>\n"
"       Loads the specified module into Asterisk.\n";

static char unload_help[] = 
"Usage: unload [-f|-h] <module name>\n"
"       Unloads the specified module from Asterisk.  The -f\n"
"       option causes the module to be unloaded even if it is\n"
"       in use (may cause a crash) and the -h module causes the\n"
"       module to be unloaded even if the module says it cannot, \n"
"       which almost always will cause a crash.\n";

static char help_help[] =
"Usage: help [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command.  If called without a\n"
"       topic, it provides a list of commands.\n";

static char chanlist_help[] = 
"Usage: show channels\n"
"       Lists currently defined channels and some information about\n"
"       them.\n";

static int handle_load(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (ast_load_resource(argv[1])) {
		ast_cli(fd, "Unable to load module %s\n", argv[1]);
		return RESULT_FAILURE;
	}
	return RESULT_SUCCESS;
}

static int handle_unload(int fd, int argc, char *argv[])
{
	int x;
	int force=AST_FORCE_SOFT;
	if (argc < 2)
		return RESULT_SHOWUSAGE;
	for (x=1;x<argc;x++) {
		if (argv[x][0] == '-') {
			switch(argv[x][1]) {
			case 'f':
				force = AST_FORCE_FIRM;
				break;
			case 'h':
				force = AST_FORCE_HARD;
				break;
			default:
				return RESULT_SHOWUSAGE;
			}
		} else if (x !=  argc - 1) 
			return RESULT_SHOWUSAGE;
		else if (ast_unload_resource(argv[x], force)) {
			ast_cli(fd, "Unable to unload resource %s\n", argv[x]);
			return RESULT_FAILURE;
		}
	}
	return RESULT_SUCCESS;
}

#define MODLIST_FORMAT  "%-20s %-40.40s %-10d\n"
#define MODLIST_FORMAT2 "%-20s %-40.40s %-10s\n"

static pthread_mutex_t climodentrylock = PTHREAD_MUTEX_INITIALIZER;
static int climodentryfd = -1;

static int modlist_modentry(char *module, char *description, int usecnt)
{
	ast_cli(climodentryfd, MODLIST_FORMAT, module, description, usecnt);
	return 0;
}

static char modlist_help[] =
"Usage: show modules\n"
"       Shows Asterisk modules currently in use, and usage "
"statistics.\n";

static int handle_modlist(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	pthread_mutex_lock(&climodentrylock);
	climodentryfd = fd;
	ast_cli(fd, MODLIST_FORMAT2, "Module", "Description", "Use Count");
	ast_update_module_list(modlist_modentry);
	climodentryfd = -1;
	pthread_mutex_unlock(&climodentrylock);
	return RESULT_SUCCESS;
}

static int handle_chanlist(int fd, int argc, char *argv[])
{
#define FORMAT_STRING  "%15s  (%-10s %-12s %-4d)  %-12s  %-15s\n"
#define FORMAT_STRING2 "%15s  (%-10s %-12s %-4s)  %-12s  %-15s\n"
	struct ast_channel *c=NULL;
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	c = ast_channel_walk(NULL);
	ast_cli(fd, FORMAT_STRING2, "Channel", "Context", "Extension", "Pri", "Appl.", "Data");
	while(c) {
		ast_cli(fd, FORMAT_STRING, c->name, c->context, c->exten, c->priority, 
		c->appl ? c->appl : "(None)", c->data ? ( strlen(c->data) ? c->data : "(Empty)" ): "(None)");
		c = ast_channel_walk(c);
	}
	return RESULT_SUCCESS;
}

static char showchan_help[] = 
"Usage: show channel <channel>\n"
"       Shows lots of information about the specified channel.\n";

static int handle_showchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	c = ast_channel_walk(NULL);
	while(c) {
		if (!strcasecmp(c->name, argv[2])) {
			ast_cli(fd, 
	" -- General --\n"
	"           Name: %s\n"
	"           Type: %s\n"
	"     Translator: %s\n"
	"         Master: %s\n"
	"      Caller ID: %s\n"
	"    DNID Digits: %s\n"
	"          State: %d\n"
	"          Rings: %d\n"
	"         Format: %d\n"
	"File Descriptor: %d\n"
	" --   PBX   --\n"
	"        Context: %s\n"
	"      Extension: %s\n"
	"       Priority: %d\n"
	"    Application: %s\n"
	"           Data: %s\n"
	"          Stack: %d\n"
	"    Blocking in: %s\n",
	c->name, c->type, (c->trans ? c->trans->name : "(N/A)"),
	(c->master ? c->master->name : "(N/A)"), (c->callerid ? c->callerid : "(N/A)"),
	(c->dnid ? c->dnid : "(N/A)" ), c->state, c->rings, c->format,
	c->fd, c->context, c->exten, c->priority, ( c->appl ? c->appl : "(N/A)" ),
	( c-> data ? (strlen(c->data) ? c->data : "(Empty)") : "(None)"),
	c->stack, (c->blocking ? c->blockproc : "(Not Blocking)"));
	
		break;
		}
		c = ast_channel_walk(c);
	}
	if (!c) 
		ast_cli(fd, "%s is not a known channel\n", argv[2]);
	return RESULT_SUCCESS;
}

static char *complete_ch(char *line, char *word, int pos, int state)
{
	struct ast_channel *c;
	int which=0;
	c = ast_channel_walk(NULL);
	while(c) {
		if (++which > state)
			break;
		c = ast_channel_walk(c);
	}
	return c ? strdup(c->name) : NULL;
}

static char *complete_fn(char *line, char *word, int pos, int state)
{
	char *c;
	char filename[256];
	if (pos != 1)
		return NULL;
	if (word[0] == '/')
		strncpy(filename, word, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", AST_MODULE_DIR, word);
	c = filename_completion_function(filename, state);
	if (c && word[0] != '/')
		c += (strlen(AST_MODULE_DIR) + 1);
	return c ? strdup(c) : c;
}

static int handle_help(int fd, int argc, char *argv[]);

static struct ast_cli_entry builtins[] = {
	/* Keep alphabetized */
	{ { "help", NULL }, handle_help, "Display help list, or specific help on a command", help_help },
	{ { "load", NULL }, handle_load, "Load a dynamic module by name", load_help, complete_fn },
	{ { "show", "channel", NULL }, handle_showchan, "Display information on a specific channel", showchan_help, complete_ch },
	{ { "show", "channels", NULL }, handle_chanlist, "Display information on channels", chanlist_help },
    { { "show", "modules", NULL }, handle_modlist, "List modules and info", modlist_help },
	{ { "unload", NULL }, handle_unload, "Unload a dynamic module by name", unload_help, complete_fn },
	{ { NULL }, NULL, NULL, NULL }
};

static struct ast_cli_entry *find_cli(char *cmds[], int exact)
{
	int x;
	int y;
	int match;
	struct ast_cli_entry *e=NULL;
	for (x=0;builtins[x].cmda[0];x++) {
		/* start optimistic */
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			/* If there are no more words in the candidate command, then we're
			   there.  */
			if (!builtins[x].cmda[y] && !exact)
				break;
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!builtins[x].cmda[y] || strcasecmp(builtins[x].cmda[y], cmds[y]))
				match = 0;
		}
		/* If more words are needed to complete the command then this is not
		   a candidate (unless we're looking for a really inexact answer  */
		if ((exact > -1) && builtins[x].cmda[y])
			match = 0;
		if (match)
			return &builtins[x];
	}
	for (e=helpers;e;e=e->next) {
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			if (!e->cmda[y] && !exact)
				break;
			if (!e->cmda[y] || strcasecmp(e->cmda[y], cmds[y]))
				match = 0;
		}
		if ((exact > -1) && e->cmda[y])
			match = 0;
		if (match)
			break;
	}
	return e;
}

static void join(char *s, int len, char *w[])
{
	int x;
	/* Join words into a string */
	strcpy(s, "");
	for (x=0;w[x];x++) {
		if (x)
			strncat(s, " ", len - strlen(s));
		strncat(s, w[x], len - strlen(s));
	}
}

static void join2(char *s, int len, char *w[])
{
	int x;
	/* Join words into a string */
	strcpy(s, "");
	for (x=0;w[x];x++) {
		strncat(s, w[x], len - strlen(s));
	}
}

static char *find_best(char *argv[])
{
	static char cmdline[80];
	int x;
	/* See how close we get, then print the  */
	char *myargv[AST_MAX_CMD_LEN];
	for (x=0;x<AST_MAX_CMD_LEN;x++)
		myargv[x]=NULL;
	for (x=0;argv[x];x++) {
		myargv[x] = argv[x];
		if (!find_cli(myargv, -1))
			break;
	}
	join(cmdline, sizeof(cmdline), myargv);
	return cmdline;
}

int ast_cli_unregister(struct ast_cli_entry *e)
{
	struct ast_cli_entry *cur, *l=NULL;
	pthread_mutex_lock(&clilock);
	cur = helpers;
	while(cur) {
		if (e == cur) {
			/* Rewrite */
			if (l)
				l->next = e->next;
			else
				helpers = e->next;
			e->next = NULL;
			break;
		}
		l = cur;
		cur = cur->next;
	}
	pthread_mutex_unlock(&clilock);
	return 0;
}

int ast_cli_register(struct ast_cli_entry *e)
{
	struct ast_cli_entry *cur, *l=NULL;
	char fulle[80], fulltst[80];
	static int len;
	pthread_mutex_lock(&clilock);
	join2(fulle, sizeof(fulle), e->cmda);
	if (find_cli(e->cmda, -1)) {
		ast_log(LOG_WARNING, "Command '%s' already registered (or something close enough)\n", fulle);
		pthread_mutex_unlock(&clilock);
		return -1;
	}
	cur = helpers;
	while(cur) {
		join2(fulltst, sizeof(fulltst), cur->cmda);
		len = strlen(fulltst);
		if (strlen(fulle) < len)
			len = strlen(fulle);
		if (strncasecmp(fulle, fulltst, len) < 0) {
			if (l) {
				e->next = l->next;
				l->next = e;
			} else {
				e->next = helpers;
				helpers = e;
			}
			break;
		}
		l = cur;
		cur = cur->next;
	}
	if (!cur) {
		if (l)
			l->next = e;
		else
			helpers = e;
		e->next = NULL;
	}
	pthread_mutex_unlock(&clilock);
	return 0;
}

static int help_workhorse(int fd, char *match[])
{
	char fullcmd1[80];
	char fullcmd2[80];
	char matchstr[80];
	char *fullcmd;
	struct ast_cli_entry *e, *e1, *e2;
	e1 = builtins;
	e2 = helpers;
	if (match)
		join(matchstr, sizeof(matchstr), match);
	while(e1->cmda[0] || e2) {
		if (e2)
			join(fullcmd2, sizeof(fullcmd2), e2->cmda);
		if (e1->cmda[0])
			join(fullcmd1, sizeof(fullcmd1), e1->cmda);
		if (!e1->cmda[0] || 
				(e2 && (strcmp(fullcmd2, fullcmd1) < 0))) {
			/* Use e2 */
			e = e2;
			fullcmd = fullcmd2;
			/* Increment by going to next */
			e2 = e2->next;
		} else {
			/* Use e1 */
			e = e1;
			fullcmd = fullcmd1;
			e1++;
		}
		if (match) {
			if (strncasecmp(matchstr, fullcmd, strlen(matchstr))) {
				continue;
			}
		}
		ast_cli(fd, "%20.20s   %s\n", fullcmd, e->summary);
	}
	return 0;
}

static int handle_help(int fd, int argc, char *argv[]) {
	struct ast_cli_entry *e;
	char fullcmd[80];
	if ((argc < 1))
		return RESULT_SHOWUSAGE;
	if (argc > 1) {
		e = find_cli(argv + 1, 1);
		if (e) 
			ast_cli(fd, e->usage);
		else {
			if (find_cli(argv + 1, -1)) {
				return help_workhorse(fd, argv + 1);
			} else {
				join(fullcmd, sizeof(fullcmd), argv+1);
				ast_cli(fd, "No such command '%s'.\n", fullcmd);
			}
		}
	} else {
		return help_workhorse(fd, NULL);
	}
	return RESULT_SUCCESS;
}

static char *parse_args(char *s, int *max, char *argv[])
{
	char *dup, *cur;
	int x=0;
	int quoted=0;
	int escaped=0;
	int whitespace=1;

	dup = strdup(s);
	if (dup) {
		cur = dup;
		while(*s) {
			switch(*s) {
			case '"':
				/* If it's escaped, put a literal quote */
				if (escaped) 
					goto normal;
				else 
					quoted = !quoted;
				escaped = 0;
				break;
			case ' ':
			case '\t':
				if (!quoted && !escaped) {
					/* If we're not quoted, mark this as whitespace, and
					   end the previous argument */
					whitespace = 1;
					*(cur++) = '\0';
				} else
					/* Otherwise, just treat it as anything else */ 
					goto normal;
				break;
			case '\\':
				/* If we're escaped, print a literal, otherwise enable escaping */
				if (escaped) {
					goto normal;
				} else {
					escaped=1;
				}
				break;
			default:
normal:
				if (whitespace) {
					if (x >= AST_MAX_ARGS -1) {
						ast_log(LOG_WARNING, "Too many arguments, truncating\n");
						break;
					}
					/* Coming off of whitespace, start the next argument */
					argv[x++] = cur;
					whitespace=0;
				}
				*(cur++) = *s;
				escaped=0;
			}
			s++;
		}
		/* Null terminate */
		*(cur++) = '\0';
		argv[x] = NULL;
		*max = x;
	}
	return dup;
}

char *ast_cli_generator(char *text, char *word, int state)
{
	char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e, *e1, *e2;
	int x;
	int matchnum=0;
	char *dup, *res;
	char fullcmd1[80];
	char fullcmd2[80];
	char matchstr[80];
	char *fullcmd;

	if ((dup = parse_args(text, &x, argv))) {
		join(matchstr, sizeof(matchstr), argv);
		pthread_mutex_lock(&clilock);
		e1 = builtins;
		e2 = helpers;
		while(e1->cmda[0] || e2) {
			if (e2)
				join(fullcmd2, sizeof(fullcmd2), e2->cmda);
			if (e1->cmda[0])
				join(fullcmd1, sizeof(fullcmd1), e1->cmda);
			if (!e1->cmda || 
					(e2 && (strcmp(fullcmd2, fullcmd1) < 0))) {
				/* Use e2 */
				e = e2;
				fullcmd = fullcmd2;
				/* Increment by going to next */
				e2 = e2->next;
			} else {
				/* Use e1 */
				e = e1;
				fullcmd = fullcmd1;
				e1++;
			}
			if (!strncasecmp(matchstr, fullcmd, strlen(matchstr))) {
				/* We contain the first part of one or more commands */
				matchnum++;
				if (matchnum > state) {
					/* Now, what we're supposed to return is the next word... */
					if (strlen(word)) {
						res = e->cmda[x-1];
					} else {
						res = e->cmda[x];
					}
					if (res) {
						pthread_mutex_unlock(&clilock);
						return res ? strdup(res) : NULL;
					}
				}
			}
			if (e->generator && !strncasecmp(matchstr, fullcmd, strlen(fullcmd))) {
				/* We have a command in its entirity within us -- theoretically only one
				   command can have this occur */
				fullcmd = e->generator(text, word, (strlen(word) ? (x - 1) : (x)), state);
				pthread_mutex_unlock(&clilock);
				return fullcmd;
			}
			
		}
		pthread_mutex_unlock(&clilock);
		free(dup);
	}
	return NULL;
}

int ast_cli_command(int fd, char *s)
{
	char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e;
	int x;
	char *dup;
	x = AST_MAX_ARGS;
	if ((dup = parse_args(s, &x, argv))) {
		/* We need at least one entry, or ignore */
		if (x > 0) {
			pthread_mutex_lock(&clilock);
			e = find_cli(argv, 0);
			if (e) {
				switch(e->handler(fd, x, argv)) {
				case RESULT_SHOWUSAGE:
					ast_cli(fd, e->usage);
					break;
				default:
				}
			} else 
				ast_cli(fd, "No such command '%s' (type 'help' for help)\n", find_best(argv));
			pthread_mutex_unlock(&clilock);
		}
		free(dup);
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");	
		return -1;
	}
	return 0;
}
