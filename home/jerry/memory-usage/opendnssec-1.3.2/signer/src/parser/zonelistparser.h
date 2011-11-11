/*
 * $Id: zonelistparser.h 4685 2011-04-07 13:52:32Z matthijs $
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
 *
 * Parsing zonelist files.
 */

#ifndef PARSER_ZONELISTPARSER_H
#define PARSER_ZONELISTPARSER_H

#include "adapter/adapter.h"
#include "shared/allocator.h"
#include "shared/status.h"

#include <libxml/xpath.h>
#include <libxml/xmlreader.h>

struct zonelist_struct;

/**
 * Parse adapter.
 * \param[in] xpathCtx XPath Context Pointer
 * \param[in] expr expression
 * \param[in] inbound true if Input Adapter
 *
 */
adapter_type* parse_zonelist_adapter(xmlXPathContextPtr xpathCtx,
    xmlChar* expr, int inbound);

/**
 * Parse the zonelist file.
 * \param[in] zlist zone list storage
 * \param[in] zlfile zonelist file name
 * \return ods_status status
 *
 */
ods_status parse_zonelist_zones(struct zonelist_struct* zlist,
    const char* zlfile);

#endif /* PARSER_ZONELISTPARSER_H */
