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

#include <string.h>
#include "digcalc.h"

// Fix for the "signedness" warning
#define CUCP (const unsigned char *)
#define UCP  (unsigned char *)

void CvtHex(
  IN HASH Bin,
  OUT HASHHEX Hex
) {
  unsigned short i;
  unsigned char j;

  for (i = 0; i < HASHLEN; i++) {
    j = (Bin[i] >> 4) & 0xf;
    if (j <= 9) {
      Hex[i*2] = (j + '0');
    } else {
      Hex[i*2] = (j + 'a' - 10);
    }
    j = Bin[i] & 0xf;
    if (j <= 9) {
      Hex[i*2+1] = (j + '0');
    } else {
      Hex[i*2+1] = (j + 'a' - 10);
    }
  };
  Hex[HASHHEXLEN] = '\0';
};

/* calculate H(A1) as per spec */
void DigestCalcHA1(
  IN char * pszAlg,
  IN char * pszUserName,
  IN char * pszRealm,
  IN char * pszPassword,
  IN char * pszNonce,
  IN char * pszCNonce,
  OUT HASHHEX SessionKey
) {
  struct MD5Context Md5Ctx;
  HASH HA1;

  MD5Init(&Md5Ctx);
  MD5Update(&Md5Ctx, CUCP pszUserName, strlen(pszUserName));
  MD5Update(&Md5Ctx, CUCP ":", 1);
  MD5Update(&Md5Ctx, CUCP pszRealm, strlen(pszRealm));
  MD5Update(&Md5Ctx, CUCP ":", 1);
  MD5Update(&Md5Ctx, CUCP pszPassword, strlen(pszPassword));
  MD5Final(UCP HA1, &Md5Ctx);
  if (strcasecmp(pszAlg, "md5-sess") == 0) {
    MD5Init(&Md5Ctx);
    MD5Update(&Md5Ctx, CUCP HA1, HASHLEN);
    MD5Update(&Md5Ctx, CUCP ":", 1);
    MD5Update(&Md5Ctx, CUCP pszNonce, strlen(pszNonce));
    MD5Update(&Md5Ctx, CUCP ":", 1);
    MD5Update(&Md5Ctx, CUCP pszCNonce, strlen(pszCNonce));
    MD5Final(UCP HA1, &Md5Ctx);
  };
  CvtHex(HA1, SessionKey);
};

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
) {
  struct MD5Context Md5Ctx;
  HASH HA2;
  HASH RespHash;
  HASHHEX HA2Hex;

  // calculate H(A2)
  MD5Init(&Md5Ctx);
  MD5Update(&Md5Ctx, CUCP pszMethod, strlen(pszMethod));
  MD5Update(&Md5Ctx, CUCP ":", 1);
  MD5Update(&Md5Ctx, CUCP pszDigestUri, strlen(pszDigestUri));
  if (strcasecmp(pszQop, "auth-int") == 0) {
    MD5Update(&Md5Ctx, CUCP ":", 1);
    MD5Update(&Md5Ctx, CUCP HEntity, HASHHEXLEN);
  };
  MD5Final(UCP HA2, &Md5Ctx);
  CvtHex(HA2, HA2Hex);

  // calculate response
  MD5Init(&Md5Ctx);
  MD5Update(&Md5Ctx, CUCP HA1, HASHHEXLEN);
  MD5Update(&Md5Ctx, CUCP ":", 1);
  MD5Update(&Md5Ctx, CUCP pszNonce, strlen(pszNonce));
  MD5Update(&Md5Ctx, CUCP ":", 1);
  if (*pszQop) {
    MD5Update(&Md5Ctx, CUCP pszNonceCount, strlen(pszNonceCount));
    MD5Update(&Md5Ctx, CUCP ":", 1);
    MD5Update(&Md5Ctx, CUCP pszCNonce, strlen(pszCNonce));
    MD5Update(&Md5Ctx, CUCP ":", 1);
    MD5Update(&Md5Ctx, CUCP pszQop, strlen(pszQop));
    MD5Update(&Md5Ctx, CUCP ":", 1);
  };
  MD5Update(&Md5Ctx, CUCP HA2Hex, HASHHEXLEN);
  MD5Final(UCP RespHash, &Md5Ctx);
  CvtHex(RespHash, Response);
};
