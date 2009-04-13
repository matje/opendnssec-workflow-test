#ifndef KMEDEF_H
#define KMEDEF_H

/*+
 * kmedef.h - Define KSM Error Codes
 *
 * Description:
 *      Defines the various status codes that can be returned by the various
 *      KSM routines.
 *
 *      All status codes - with the exception of KME_SUCCESS - are above
 *      65,536.  Below this, status values are assumed to be error values
 *      returned from the operating system.
 *
 * Copyright:
 *      Copyright 2008 Nominet
 *      
 * Licence:
 *      Licensed under the Apache Licence, Version 2.0 (the "Licence");
 *      you may not use this file except in compliance with the Licence.
 *      You may obtain a copy of the Licence at
 *      
 *          http://www.apache.org/licenses/LICENSE-2.0
 *      
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the Licence is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the Licence for the specific language governing permissions and
 *      limitations under the Licence.
-*/

#define KME_SUCCESS 0                       /* Successful completion */

#define KME_BASE        65536               /* Base of KSM status codes */

#define KME_ACTKEYRET   (KME_BASE +  0)     /* INFO: %d keys in 'active' state will have their expected retire date modified */
#define KME_AVAILCNT    (KME_BASE +  1)     /* INFO: %d keys current in 'publish', 'ready' and 'active' states */
#define KME_BUFFEROVF   (KME_BASE +  2)     /* ERROR: internal error, buffer overflow in %s */
#define KME_CHILDREN    (KME_BASE +  3)     /* ERROR: unable to delete %s because child objects are associated with it */
#define KME_CREFAIL     (KME_BASE +  4)     /* ERROR: failed to create '%s' */
#define KME_EXISTS      (KME_BASE +  5)     /* ERROR: object with name '%s' already exists */
#define KME_FLDMISMAT   (KME_BASE +  6)     /* ERROR: program error - number of fields returned did not match number expected */
#define KME_GENERATECNT (KME_BASE +  7)     /* INFO: %d %ss available in 'generate' state */
#define KME_INSFGENKEY  (KME_BASE +  8)     /* ERROR: only %d %ss available in 'generate' state - request abandoned */
#define KME_KEYCHSTATE  (KME_BASE +  9)     /* INFO: moving %d key(s) from '%s' state to '%s' state */
#define KME_KEYCNTSUMM  (KME_BASE + 10)     /* INFO: %d keys required, therefore %d new keys need to be put in 'publish' state */
#define KME_NOREADYKEY  (KME_BASE + 11)     /* WARNING: cannot continue with key rollover as there are no keys in the 'ready' state */
#define KME_NOSUCHPAR   (KME_BASE + 12)     /* ERROR: no such parameter with name %s */
#define KME_NOTFOUND    (KME_BASE + 13)     /* ERROR: unable to find object '%s' */
#define KME_NOTIMPL     (KME_BASE + 14)     /* WARNING: Command not implemented yet */
#define KME_NOTZONE     (KME_BASE + 15)     /* ERROR: %s is not a zone */
#define KME_PERMANENT   (KME_BASE + 16)     /* ERROR: it is not permitted to delete the permanent object %s */
#define KME_READYCNT    (KME_BASE + 17)     /* INFO: %d %ss in the 'ready' state */
#define KME_REMAINACT   (KME_BASE + 18)     /* INFO: %d %ss remaining in 'active' state */
#define KME_REQKEYTYPE  (KME_BASE + 19)     /* INFO: requesting issue of %s signing keys */
#define KME_RETIRECNT   (KME_BASE + 20)     /* INFO: %d 'active' keys will be retiring in the immediate future */
#define KME_SQLFAIL     (KME_BASE + 21)     /* ERROR: database operation failed - %s */
#define KME_UNKEYTYPE	(KME_BASE + 22)		/* ERROR: unknown key type, code %d */
#define KME_UNRCONCOD   (KME_BASE + 23)     /* WARNING: unrecognised condition code %d: code ignored */
#define KME_UNRKEYSTA   (KME_BASE + 24)     /* WARNING: key ID %d is in unrecognised state %d */


#endif
