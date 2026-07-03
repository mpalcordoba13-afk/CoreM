#ifndef TLS_H
#define TLS_H
/*
 * tls.h  –  Cliente TLS 1.2 mínimo para MyOS
 *
 * Cipher suite: TLS_RSA_WITH_AES_128_CBC_SHA (0x002F)
 * Sin verificacion de certificado (solo cifrado en tránsito).
 *
 * Uso:
 *   int n = net_https_get("example.com", "/", buf, sizeof(buf));
 *   // n >= 0: bytes del cuerpo HTTP; n < 0: error
 */

/* Descarga https://host/path, igual que net_http_get pero con TLS.
 * Devuelve bytes del cuerpo escritos en out, o -1 si fallo. */
int net_https_get(const char *host, const char *path, char *out, int maxlen);

#endif
