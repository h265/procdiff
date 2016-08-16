/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*-  */
/*
 * procdiff.c
 * Copyright (C) 2016 Luala
 *
 */
#ifndef PROC_DIFF_H
#define PROC_DIFF_H


int major_ver = 0;
int minor_ver = 1;

typedef struct proc_item {
    int     pid;                 /* process id */
    int     ppid;                /* parent process id */
    char    name[1024];          /* process name, read from 'Name:" from /proc/<pid>/status */
    struct proc_item* next;
} proc_item;

typedef struct proc_list {
    proc_item *head;
    proc_item *tail;
    size_t size;
} proc_list;


#endif //PROC_DIFF_H
