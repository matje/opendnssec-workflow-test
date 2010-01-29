/*
 * $Id$
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
 * Inbound and Outbound Adapters.
 *
 */

#include "adapter/adapter.h"
#include "util/file.h"
#include "util/log.h"
#include "util/se_malloc.h"

#include <stdio.h>

static const char* adapter_type2str(adapter_mode type);


/**
 * Create a new adapter.
 *
 */
adapter_type*
adapter_create(const char* filename, adapter_mode type, int inbound)
{
    adapter_type* adapter = (adapter_type*) se_malloc(sizeof(adapter_type));

    se_log_assert(filename);
    se_log_debug3("create %s adapter: %s[%i]",
        inbound?"inbound":"outbound", filename, (int) type);

    adapter->filename = se_strdup(filename);
    adapter->type = type;
    adapter->inbound = inbound;
    return adapter;
}


/**
 * Clean up adapter.
 *
 */
void
adapter_cleanup(adapter_type* adapter)
{
    se_log_debug3("clean up adapter");

    if (adapter) {
/*
        se_log_debug3("clean up adapter filename");
        se_log_debug3("clean up adapter pointer");
*/
        se_free((void*)adapter->filename);
        se_free((void*)adapter);
        se_log_debug3("clean up adapter: done");
    } else {
        se_log_debug3("clean up adapter: null");
    }
}

/*
 * Human readable adapter type.
 *
 */
static const char*
adapter_type2str(adapter_mode type)
{
    switch (type) {
        case 1:
            return "File";
            break;
        case 0:
        default:
            return "Unknown";
            break;
    }
    return "Unknown";
}


/**
 * Compare adapters.
 *
 */
int
adapter_compare(adapter_type* a1, adapter_type* a2)
{
    se_log_none("compare adapters");
    se_log_assert(a1);
    se_log_assert(a2);

    if (!a1 && !a2) {
        return 0;
    } else if (!a1) {
        return -1;
    } else if (!a2) {
        return 1;
    } else if (a1->inbound != a2->inbound) {
        return a1->inbound - a2->inbound;
    } else if (a1->type != a2->type) {
        return a1->type - a2->type;
    }
    return se_strcmp(a1->filename, a2->filename);
}


/**
 * Print adapter.
 *
 */
void
adapter_print(FILE* fd, adapter_type* adapter)
{
    se_log_debug3("print adapter");
    if (adapter) {
        fprintf(fd, "\t\t\t<%s>\n", adapter->inbound?"Input":"Output");
        fprintf(fd, "\t\t\t\t<%s>%s</%s>\n",
            adapter_type2str(adapter->type), adapter->filename,
            adapter_type2str(adapter->type));
        fprintf(fd, "\t\t\t</%s>\n", adapter->inbound?"Input":"Output");
    }
    se_log_debug3("print adapter: done");
}
