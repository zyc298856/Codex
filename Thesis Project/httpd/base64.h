#ifndef _BASE64_H
#define _BASE64_H

#ifdef __cplusplus
extern "C"
{
#endif

int base64_encode(const unsigned char * sourcedata, unsigned char * base64,int datalengthin);
int base64_decode(const char * base64, unsigned char * dedata);

#ifdef __cplusplus
}
#endif

#endif