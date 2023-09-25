/*
Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.
Licensed under the WiPhone Public License v.1.0 (the "License"); you
may not use this file except in compliance with the License. You may
obtain a copy of the License at
https://wiphone.io/WiPhone_Public_License_v1.0.txt.

Unless required by applicable law or agreed to in writing, software,
hardware or documentation distributed under the License is distributed
on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

/*
 * Reference Digest implementation from RFC 2617
 */

#if !defined(_DIGCALC_H_)
#define _DIGCALC_H_

#include "rom/md5_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HASHLEN 16
typedef char HASH[HASHLEN];
#define HASHHEXLEN 32
typedef char HASHHEX[HASHHEXLEN+1];
#define IN
#define OUT

void CvtHex(IN HASH Bin, OUT HASHHEX Hex);

/* calculate H(A1) as per HTTP Digest spec */
void DigestCalcHA1(
  IN char * pszAlg,
  IN char * pszUserName,
  IN char * pszRealm,
  IN char * pszPassword,
  IN char * pszNonce,
  IN char * pszCNonce,
  OUT HASHHEX SessionKey
);

/* calculate request-digest/response-digest as per HTTP Digest spec */
void DigestCalcResponse(
  IN HASHHEX HA1,           /* H(A1) */
  IN char * pszNonce,       /* nonce from server */
  IN char * pszNonceCount,  /* 8 hex digits */
  IN char * pszCNonce,      /* client nonce */
  IN char * pszQop,         /* qop-value: "", "auth", "auth-int" */
  IN char * pszMethod,      /* method from the request */
  IN char * pszDigestUri,   /* requested URL */
  IN HASHHEX HEntity,       /* H(entity body) if qop="auth-int" */
  OUT HASHHEX Response      /* request-digest or response-digest */
);

#ifdef __cplusplus
}
#endif

#endif // _DIGCALC_H_
