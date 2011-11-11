/*
 * $Id: task.c 4613 2011-03-22 07:54:50Z rb $
 *
 * Copyright (c) 2009 NLNet Labs. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * Tasks.
 *
 */

#include "config.h"
#include "mm.h"
#include "scheduler/task.h"
#include "shared/allocator.h"
#include "shared/duration.h"
#include "shared/file.h"
#include "shared/log.h"
#include "signer/backup.h"

static const char* task_str = "task";


/**
 * Create a new task.
 *
 */
task_type*
task_create(task_id what, time_t when, const char* who, void* zone)
{
    allocator_type* allocator = NULL;
    task_type* task = NULL;

    if (!who || !zone) {
        ods_log_error("[%s] cannot create: missing zone info", task_str);
        return NULL;
    }
    ods_log_assert(who);
    ods_log_assert(zone);

    allocator = allocator_create(mm_malloc2, mm_free2);
    if (!allocator) {
        ods_log_error("[%s] cannot create: create allocator failed", task_str);
        return NULL;
    }
    ods_log_assert(allocator);

    task = (task_type*) allocator_alloc(allocator, sizeof(task_type));
    if (!task) {
        ods_log_error("[%s] cannot create: allocator failed", task_str);
        allocator_cleanup(allocator);
        return NULL;
    }
    task->allocator = allocator;
    task->what = what;
    task->interrupt = TASK_NONE;
    task->halted = TASK_NONE;
    task->when = when;
    task->backoff = 0;
    task->who = allocator_strdup(allocator, who);
    task->dname = ldns_dname_new_frm_str(who);
    task->flush = 0;
    task->zone = zone;
    return task;
}


/**
 * Recover a task from backup.
 *
 */
task_type*
task_recover_from_backup(const char* filename, void* zone)
{
    task_type* task = NULL;
    FILE* fd = NULL;
    const char* who = NULL;
    int what = 0;
    time_t when = 0;
    int flush = 0;
    time_t backoff = 0;

    ods_log_assert(zone);
    fd = ods_fopen(filename, NULL, "r");
    if (fd) {
        if (!backup_read_check_str(fd, ODS_SE_FILE_MAGIC) ||
            !backup_read_check_str(fd, ";who:") ||
            !backup_read_str(fd, &who) ||
            !backup_read_check_str(fd, ";what:") ||
            !backup_read_int(fd, &what) ||
            !backup_read_check_str(fd, ";when:") ||
            !backup_read_time_t(fd, &when) ||
            !backup_read_check_str(fd, ";flush:") ||
            !backup_read_int(fd, &flush) ||
            !backup_read_check_str(fd, ";backoff:") ||
            !backup_read_time_t(fd, &backoff) ||
            !backup_read_check_str(fd, ODS_SE_FILE_MAGIC))
        {
            ods_log_error("[%s] unable to recover task from file %s: file corrupted",
                task_str, filename?filename:"(null)");
            task = NULL;
        } else {
            task = task_create((task_id) what, when, who, (void*) zone);
            task->flush = flush;
            task->backoff = backoff;
        }
        mm_free((void*)who);
        ods_fclose(fd);
        return task;
    }

    ods_log_debug("[%s] unable to recover task from file %s: no such file or directory",
        task_str, filename?filename:"(null)");
    return NULL;
}


/**
 * Backup task.
 *
 */
void
task_backup(FILE* fd, task_type* task)
{
    if (!fd || !task) {
        return;
    }
    ods_log_assert(fd);
    ods_log_assert(task);

    fprintf(fd, ";;Task: when %u what %i interrupt %i halted %i backoff %i "
        "flush %i\n",
        (unsigned) task->when,
        (int) task->what,
        (int) task->interrupt,
        (int) task->halted,
        (unsigned) task->backoff,
        task->flush);
    return;
}


/**
 * Clean up task.
 *
 */
void
task_cleanup(task_type* task)
{
    allocator_type* allocator;

    if (!task) {
        return;
    }
    allocator = task->allocator;
    if (task->dname) {
        ldns_rdf_deep_free(task->dname);
        task->dname = NULL;
    }
    allocator_deallocate(allocator, (void*) task->who);
    allocator_deallocate(allocator, (void*) task);
    allocator_cleanup(allocator);
    return;
}


/**
 * Compare tasks.
 *
 */
int
task_compare(const void* a, const void* b)
{
    task_type* x = (task_type*)a;
    task_type* y = (task_type*)b;

    ods_log_assert(x);
    ods_log_assert(y);

    if (!ldns_dname_compare((const void*) x->dname, (const void*) y->dname)) {
        /* if dname is the same, consider the same task */
        return 0;
    }

    /* order task on time, what to do, dname */
    if (x->when != y->when) {
        return (int) x->when - y->when;
    }
    if (x->what != y->what) {
        return (int) x->what - y->what;
    }
    return ldns_dname_compare((const void*) x->dname, (const void*) y->dname);
}


/**
 * String-format of what.
 *
 */
const char*
task_what2str(int what)
{
    switch (what) {
        case TASK_NONE:
            return "[do nothing with]";
            break;
        case TASK_SIGNCONF:
            return "[load signconf for]";
            break;
        case TASK_READ:
            return "[read]";
            break;
        case TASK_NSECIFY:
            return "[nsecify]";
            break;
        case TASK_SIGN:
            return "[sign]";
            break;
        case TASK_AUDIT:
            return "[audit]";
            break;
        case TASK_WRITE:
            return "[write]";
            break;
        default:
            return "[???]";
            break;
    }
    return "[???]";
}


/**
 * String-format of who.
 *
 */
const char*
task_who2str(const char* who)
{
    if (who) {
        return who;
    }
    return "(null)";
}


/**
 * Convert task to string.
 *
 */
char*
task2str(task_type* task, char* buftask)
{
    char* strtime = NULL;
    char* strtask = NULL;

    if (task) {
        strtime = ctime(&task->when);
        if (strtime) {
            strtime[strlen(strtime)-1] = '\0';
        }
        if (buftask) {
            (void)snprintf(buftask, ODS_SE_MAXLINE, "%s %s I will %s zone %s"
                "\n", task->flush?"Flush":"On", strtime?strtime:"(null)",
                task_what2str(task->what), task_who2str(task->who));
            return buftask;
        } else {
            strtask = (char*) mm_calloc(ODS_SE_MAXLINE, sizeof(char));
            snprintf(strtask, ODS_SE_MAXLINE, "%s %s I will %s zone %s\n",
                task->flush?"Flush":"On", strtime?strtime:"(null)",
                task_what2str(task->what), task_who2str(task->who));
            return strtask;
        }
    }
    return NULL;
}


/**
 * Print task.
 *
 */
void
task_print(FILE* out, task_type* task)
{
    char* strtime = NULL;

    if (out && task) {
        strtime = ctime(&task->when);
        if (strtime) {
            strtime[strlen(strtime)-1] = '\0';
        }
        fprintf(out, "%s %s I will %s zone %s\n",
            task->flush?"Flush":"On", strtime?strtime:"(null)",
            task_what2str(task->what), task_who2str(task->who));
    }
    return;
}


/**
 * Log task.
 *
 */
void
task_log(task_type* task)
{
    char* strtime = NULL;

    if (task) {
        strtime = ctime(&task->when);
        if (strtime) {
            strtime[strlen(strtime)-1] = '\0';
        }
        ods_log_debug("[%s] %s %s I will %s zone %s", task_str,
            task->flush?"Flush":"On", strtime?strtime:"(null)",
            task_what2str(task->what), task_who2str(task->who));
    }
    return;
}
