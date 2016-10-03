/* -*- Mode: C; tab-width: 4 -*-  */
/*
 * procdiff.c
 * Copyright (C) 2016 LLLfff 
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h> /* basename(.) */
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "procdiff.h"

/* myself */
static char* program_name;

/* '/proc' dir file system */
DIR *procfs;


/* Simple list implementation */
//
static void list_reset(proc_list *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

/* Add proc item to the list tail */
static proc_item* list_add_proc(proc_list *list, proc_item *p)
{

    if (list->head == NULL) {
        list->head = list->tail = p;
    } else {
        list->tail->next = p;
        list->tail = p;
    }

    list->size++;
    return (p);
}

/* free whole list memory */
static void list_clear(proc_list *list)
{
    while (list->head) {
        proc_item *p = list->head;
        list->head = p->next;
        free(p);
    }
    list_reset(list);
}

/*
 * print out all process pid and name in the list
 */
static void list_print(proc_list *list)
{
    proc_item *p = list->head;
    printf("PID\tPPID\tName\n");
    while (p) {
        printf("%d\t%d\t%s\n", p->pid, p->ppid, p->name);
        p = p->next;
    }
    printf("Total process number: %lu\n", list->size);
}

/*
 * Print items in lista but not in listb
 */
static void print_list_diff(proc_list *lista, proc_list *listb)
{
    proc_item *pa = lista->head;
    while (pa) {
        char found = 0;
        proc_item *pb = listb->head;
        for (; pb; pb = pb->next) {
            if (pa->pid == pb->pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            printf("%d - %s\n", pa->pid, pa->name);
        }
        pa = pa->next;
    }
}

/*
 * Parse helper
 */
enum attr_type
{
    ATTR_NAME,
    ATTR_PID,
    ATTR_PPID,
    ATTR_UNKNOWN /*  is also used as number of known attributes */
};

/*
 * Load file content to the buffer, out of buffer are omitted
 * NOTE: no pointer sanity check
 */
static int file_to_buffer(const char *path, char *buf, int len) {

    int size;                /* context size read*/
    int fd;

    fd = open(path, O_RDONLY, 0);
    if (fd == -1) {
        return -1;
    }
    size = read(fd, buf, len - 1);
    close(fd);
    if (size <= 0) {
        return -1;
    }
    buf[size] = '\0';

    return size;
}

static int attr_to_type(const char *name, int len)
{
    if (memcmp("Name", name, len) == 0) {
        return ATTR_NAME;
    } else if (memcmp("Pid", name, len) == 0) {
        return ATTR_PID;
    } else if (memcmp("PPid", name, len) == 0) {
        return ATTR_PPID;
    }
    return ATTR_UNKNOWN;
}

/**
 *
 * Parse status file attributes to proc_item
 */
static int read_status_file(const char *dir, proc_item *p)
{
    static struct stat st;
    static char   buf[2048];             /* buffer content of /proc/<pid>/status */
    static char   path[24];              /* should be sufficient to store /proc/<pid>/status file*/
    size_t size = 0;                     /* context size read */

    sprintf(path, "%s/status", dir);

    if ((stat(path, &st) == -1)) {        /* process could be gone */
        return -1;
    }

    size = file_to_buffer(path, buf, sizeof buf);

    if (size <= 0) {
        return -1;
    }

    int att_cnt = 0;
    char* str = buf;
    for (; att_cnt < ATTR_UNKNOWN;) {

        char *colon = strchr(str, ':');
        if (!colon) break;
        if (colon[1] != '\t') break;
        int attr_type = attr_to_type(str, colon - str);
        if (attr_type == ATTR_UNKNOWN) goto next_line;
        str = colon + 2; // past the '\t'
        switch (attr_type) {
            case ATTR_NAME: {
                unsigned u = 0;
                while (u < sizeof p->name - 1u) {
                    int c = *str++;
                    if (c == '\n') break;
                    if (c == '\0') break; // should never happen
                    if (c == '\\') {
                        c = *str++;
                        if (c == '\n') break; // should never happen
                        if (!c) break; // should never happen
                        if (c == 'n') c = '\n'; // else we assume it is '\\'
                    }
                    p->name[u++] = c;
                }
                p->name[u] = '\0';
                str--;   // put back the '\n' or '\0'
                att_cnt++;
                break;
            }
            case ATTR_PID: {
                str = colon + 2; // past the '\t'
                p->pid = strtol(str, &str, 10);
                att_cnt++;
                break;
            }
            case ATTR_PPID: {
                str = colon + 2; // past the '\t'
                p->ppid = strtol(str, &str, 10);
                att_cnt++;
                break;
            }
            default:
                break;
        }
next_line:
        // advance to next line
        str = strchr(str, '\n');
        if (!str) break;  // give up if no newline
        str++;
        if (!*str) break;
    }

    return 0;
}

/*
 * Open and traverse /proc directory
 */
static int read_proc(proc_list *list)
{
    procfs = opendir("/proc");
    if (!procfs) {
        fprintf(stderr, "Error: can not access /proc, lasterror: %d.\n", errno);
        return 1;
    }

    for (;;) {

        static struct dirent *ent;
        char          path[128];
        /* find one process and its path*/
        for (;;) {
            ent = readdir(procfs);
            if ((!ent) || (!ent->d_name))
                return 0;
            /* we only need numbers which is process*/
            if (*ent->d_name > '0' && *ent->d_name <= '9')
                break;
        }

        /*
         * pid can be read from here or status file
         * pid = strtoul(ent->d_name, NULL, 10);
         */
        memset(path, 0, 128);
        memcpy(path, "/proc/", 6);
        /* should we trust /proc so not to have stack overwrite? */
        strcpy(path + 6, ent->d_name);

        /* read /proc/<pid>/status file for attributes
         * currently only reads 'Pid','PPid' and 'Name:'
         */
        proc_item *p = malloc(sizeof(proc_item));
        p->next = NULL;
        int result = read_status_file(path, p);
        if (result == -1) {
            free(p);
            return -1;
        }

        list_add_proc(list, p);
    }
}

static void close_proc()
{
    if (procfs) {
        closedir(procfs);
    }
}


static struct option long_options[] = {
        {"interval", 1, 0, 'i'},
        {"debug",    0, 0, 'd'},
        {"version",  0, 0, 'v'},
        {"help",     0, 0, 'h'},
        {0},
};

static void display_version(void)
{
    fprintf(stdout, "%s version  %d.%d\n", program_name, major_ver, minor_ver);
}

static void show_usage(void)
{
    fprintf(stdout, "Usage: %s <options>\n"
                    "Options:\n"
                    "   -i, --interval <seconds>           Configure display interval in seconds\n"
                    "                                      Minimum 1 second, default to 60 seconds\n"
                    "   -d, --debug                        Display debug information\n"
                    "   -v, --version                      Show version number\n"
                    "   -h, --help                         Display this help\n",
            program_name);
}

static void exit_program(int signo)
{
	printf("%s is stopped by signal %d\n", program_name, signo);
	
    if(signo)
        signo |= 0x80; // exit code should be (128 + signo)

    fflush(stdout);

    exit(signo);

}

int main(int argc, char *argv[])
{
    program_name = basename(argv[0]);

    int opt = 0, debug = 0, interval = 60;
	int value;
    while ((opt = getopt_long(argc, argv, "vi:dh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v' :
                display_version();
                exit(EXIT_SUCCESS);
            case 'i' :
				value = atoi(optarg);
                if (value < 1) {
                    fprintf(stderr, "Minimum interval is 1 second.\n");
                    exit(EXIT_FAILURE);
                }
				interval = value;
                break;
            case 'd' :
                debug = 1;
                break;
            case 'h' :
                show_usage();
                exit(EXIT_SUCCESS);
            default :
                break;
        }
    }
	
    signal(SIGALRM,  exit_program);
    signal(SIGHUP,   exit_program);
    signal(SIGINT,   exit_program);
    signal(SIGPIPE,  exit_program);
    signal(SIGQUIT,  exit_program);
    signal(SIGTERM,  exit_program);

    printf("Monitor processes with interval %d seconds\n", interval);
    fflush(stdout);
    //for interval timeout
    struct timespec req_ts, rm_ts;
    req_ts.tv_sec = interval;
    req_ts.tv_nsec = 0;

    //for print timestamp
    time_t t;
    struct tm* tm;

    proc_list *cur_list = NULL;
    proc_list *prev_list = NULL;

    while (1) {

        t = time(NULL);
        tm = localtime(&t);

        cur_list = malloc(sizeof (proc_list));

        list_reset(cur_list);

        read_proc(cur_list);

        if (debug) {
            list_print(cur_list);
        }

        if (prev_list != NULL) {
            printf("=====%d-%02d-%02d %02d:%02d:%02d====\n", tm->tm_year + 1900, tm->tm_mon + 1,
                   tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

            printf("Gone:\n");
            print_list_diff(prev_list, cur_list);
            printf("\n");

            printf("New:\n");
            print_list_diff(cur_list, prev_list);
            printf("\n");

            list_clear(prev_list);
            free(prev_list);

            fflush(stdout);
        }

        /* switch previous and current list */
        prev_list = cur_list;

        close_proc();

        /* use select(..) ?? */
        nanosleep(&req_ts,&rm_ts);
    }

    /* Below unreachable code, keep logic here
     * in case change the above loop with conditions
     */
    list_clear(cur_list);
    list_clear(prev_list);

    return 0;
}
