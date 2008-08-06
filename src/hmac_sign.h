/* 
 * File:   hmac_sign.h
 * Author: seangeo
 *
 * Created on August 6, 2008, 1:26 PM
 */

#ifndef _HMAC_SIGN_H
#define	_HMAC_SIGN_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <curl/curl.h>
  
extern struct curl_slist * hmac_sign(const char * method, 
                                     const char * path, 
                                     struct curl_slist *headers, 
                                     const char * access_id, 
                                     const char * secret);

#ifdef	__cplusplus
}
#endif

#endif	/* _HMAC_SIGN_H */

