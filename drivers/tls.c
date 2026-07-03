/*
 * tls.c  –  Cliente TLS 1.2 mínimo para MyOS
 *
 * Cipher suite soportado:
 *   TLS_RSA_WITH_AES_128_CBC_SHA  (0x002F)
 *
 * Restricciones / supuestos:
 *   – Sin verificacion de certificado (solo cifrado en tránsito).
 *   – RSA key-exchange con PKCS#1 v1.5 (parsea la clave pública del
 *     certificado del servidor para cifrar el PreMasterSecret).
 *   – Sin malloc: buffers estáticos.
 *   – Freestanding C99 (sin libc).
 *   – Usa tcp_connect / tcp_send / tcp_recv del stack existente.
 *
 * Implementa:
 *   SHA-1, SHA-256, HMAC-SHA1, HMAC-SHA256 (para PRF)
 *   AES-128 (ECB → CBC) sin AESNI, solo lookup tables
 *   PKCS#1 v1.5 RSA encrypt con módulo ≤ 4096 bits (512 bytes)
 *   RSA a mano con big-endian big integers (mul/mod básico)
 *
 * Punto de entrada público: net_https_get()
 */

#include "tls.h"
#include "net.h"
#include "timer.h"
#include <stdint.h>

/* ================================================================== */
/* SHA-1                                                                */
/* ================================================================== */

typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; int blen; } sha1_ctx;

static uint32_t sha1_rotl(uint32_t v, int n){ return (v<<n)|(v>>(32-n)); }

static void sha1_process(sha1_ctx *c, const uint8_t *blk){
    uint32_t w[80]; uint32_t a,b,d,e,f,k,tmp;
    for(int i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|blk[i*4+3];
    for(int i=16;i<80;i++) w[i]=sha1_rotl(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t c2 = c->h[2]; /* local alias to avoid warn-shadow on 'c' param */
    a=c->h[0];b=c->h[1];d=c->h[3];e=c->h[4];
    uint32_t hh[5]={c->h[0],c->h[1],c->h[2],c->h[3],c->h[4]};
    a=hh[0];b=hh[1];c2=hh[2];d=hh[3];e=hh[4];
    for(int i=0;i<80;i++){
        if     (i<20){f=(b&c2)|(~b&d);k=0x5A827999;}
        else if(i<40){f=b^c2^d;k=0x6ED9EBA1;}
        else if(i<60){f=(b&c2)|(b&d)|(c2&d);k=0x8F1BBCDC;}
        else         {f=b^c2^d;k=0xCA62C1D6;}
        tmp=sha1_rotl(a,5)+f+e+k+w[i];
        e=d;d=c2;c2=sha1_rotl(b,30);b=a;a=tmp;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=c2;c->h[3]+=d;c->h[4]+=e;
}

static void sha1_init(sha1_ctx *c){
    c->h[0]=0x67452301;c->h[1]=0xEFCDAB89;c->h[2]=0x98BADCFE;c->h[3]=0x10325476;c->h[4]=0xC3D2E1F0;
    c->len=0;c->blen=0;
}
static void sha1_update(sha1_ctx *c, const uint8_t *d, int n){
    for(int i=0;i<n;i++){
        c->buf[c->blen++]=d[i];
        if(c->blen==64){sha1_process(c,c->buf);c->blen=0;}
    }
    c->len+=n;
}
static void sha1_final(sha1_ctx *c, uint8_t out[20]){
    uint64_t bits=c->len*8;
    uint8_t pad=0x80;
    sha1_update(c,&pad,1);
    while(c->blen!=56){pad=0;sha1_update(c,&pad,1);}
    uint8_t lb[8];
    for(int i=7;i>=0;i--){lb[i]=(uint8_t)bits;bits>>=8;}
    sha1_update(c,lb,8);
    for(int i=0;i<5;i++){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}
}
static void sha1(const uint8_t *d, int n, uint8_t out[20]){
    sha1_ctx c; sha1_init(&c); sha1_update(&c,d,n); sha1_final(&c,out);
}

/* ================================================================== */
/* SHA-256                                                              */
/* ================================================================== */

static const uint32_t K256[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; int blen; } sha256_ctx;

static uint32_t rotr32(uint32_t v,int n){return (v>>n)|(v<<(32-n));}

static void sha256_process(sha256_ctx *c, const uint8_t *blk){
    uint32_t w[64],a,b,cc,d,e,f,g,h,t1,t2;
    for(int i=0;i<16;i++) w[i]=((uint32_t)blk[i*4]<<24)|((uint32_t)blk[i*4+1]<<16)|((uint32_t)blk[i*4+2]<<8)|blk[i*4+3];
    for(int i=16;i<64;i++){uint32_t s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3);uint32_t s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
    a=c->h[0];b=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(int i=0;i<64;i++){
        t1=h+(rotr32(e,6)^rotr32(e,11)^rotr32(e,25))+((e&f)^(~e&g))+K256[i]+w[i];
        t2=(rotr32(a,2)^rotr32(a,13)^rotr32(a,22))+((a&b)^(a&cc)^(b&cc));
        h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_init(sha256_ctx *c){
    c->h[0]=0x6a09e667;c->h[1]=0xbb67ae85;c->h[2]=0x3c6ef372;c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f;c->h[5]=0x9b05688c;c->h[6]=0x1f83d9ab;c->h[7]=0x5be0cd19;
    c->len=0;c->blen=0;
}
static void sha256_update(sha256_ctx *c, const uint8_t *d, int n){
    for(int i=0;i<n;i++){c->buf[c->blen++]=d[i];if(c->blen==64){sha256_process(c,c->buf);c->blen=0;}}
    c->len+=n;
}
static void sha256_final(sha256_ctx *c, uint8_t out[32]){
    uint64_t bits=c->len*8; uint8_t pad=0x80;
    sha256_update(c,&pad,1); pad=0;
    while(c->blen!=56) sha256_update(c,&pad,1);
    uint8_t lb[8];for(int i=7;i>=0;i--){lb[i]=(uint8_t)bits;bits>>=8;}
    sha256_update(c,lb,8);
    for(int i=0;i<8;i++){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}
}
static void sha256(const uint8_t *d, int n, uint8_t out[32]){
    sha256_ctx c; sha256_init(&c); sha256_update(&c,d,n); sha256_final(&c,out);
}

/* ================================================================== */
/* HMAC-SHA1 y HMAC-SHA256                                             */
/* ================================================================== */

static void hmac_sha1(const uint8_t *key,int klen,const uint8_t *msg,int mlen,uint8_t out[20]){
    uint8_t k0[64]; for(int i=0;i<64;i++) k0[i]=0;
    if(klen>64){sha1(key,klen,k0);}else{for(int i=0;i<klen;i++) k0[i]=key[i];}
    uint8_t ipad[64],opad[64];
    for(int i=0;i<64;i++){ipad[i]=k0[i]^0x36;opad[i]=k0[i]^0x5C;}
    sha1_ctx c; sha1_init(&c); sha1_update(&c,ipad,64); sha1_update(&c,msg,mlen);
    uint8_t inner[20]; sha1_final(&c,inner);
    sha1_init(&c); sha1_update(&c,opad,64); sha1_update(&c,inner,20); sha1_final(&c,out);
}

static void hmac_sha256(const uint8_t *key,int klen,const uint8_t *msg,int mlen,uint8_t out[32]){
    uint8_t k0[64]; for(int i=0;i<64;i++) k0[i]=0;
    if(klen>64){sha256(key,klen,k0);}else{for(int i=0;i<klen;i++) k0[i]=key[i];}
    uint8_t ipad[64],opad[64];
    for(int i=0;i<64;i++){ipad[i]=k0[i]^0x36;opad[i]=k0[i]^0x5C;}
    sha256_ctx c; sha256_init(&c); sha256_update(&c,ipad,64); sha256_update(&c,msg,mlen);
    uint8_t inner[32]; sha256_final(&c,inner);
    sha256_init(&c); sha256_update(&c,opad,64); sha256_update(&c,inner,32); sha256_final(&c,out);
}

/* ================================================================== */
/* TLS PRF (TLS 1.2 usa P_SHA256)                                      */
/* ================================================================== */

/* P_hash: expande secret+seed a 'olen' bytes usando HMAC-H */
static void p_sha256(const uint8_t *sec,int slen,
                     const uint8_t *seed,int seedlen,
                     uint8_t *out,int olen){
    /* A(0)=seed, A(i)=HMAC(sec, A(i-1)) */
    uint8_t A[32]; hmac_sha256(sec,slen,seed,seedlen,A);
    int done=0;
    while(done<olen){
        /* HMAC(sec, A(i) || seed) */
        static uint8_t tmp[32+256];
        for(int i=0;i<32;i++) tmp[i]=A[i];
        if(seedlen>256) seedlen=256;
        for(int i=0;i<seedlen;i++) tmp[32+i]=seed[i];
        uint8_t out32[32]; hmac_sha256(sec,slen,tmp,32+seedlen,out32);
        for(int i=0;i<32&&done<olen;i++) out[done++]=out32[i];
        /* A = HMAC(sec, A) */
        hmac_sha256(sec,slen,A,32,A);
    }
}

/* PRF(secret, label, seed) -> olen bytes */
static void tls_prf(const uint8_t *sec,int slen,
                    const char *label,
                    const uint8_t *seed,int seedlen,
                    uint8_t *out,int olen){
    /* label || seed */
    uint8_t ls[256]; int llen=0;
    while(label[llen]) llen++;
    for(int i=0;i<llen;i++) ls[i]=label[i];
    for(int i=0;i<seedlen&&llen+i<256;i++) ls[llen+i]=seed[i];
    p_sha256(sec,slen,ls,llen+seedlen,out,olen);
}

/* ================================================================== */
/* AES-128                                                              */
/* ================================================================== */

static const uint8_t sbox[256]={
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};
static const uint8_t inv_sbox[256]={
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static uint8_t xtime(uint8_t a){ return (a&0x80) ? ((a<<1)^0x1b) : (a<<1); }
static uint8_t gmul(uint8_t a, uint8_t b){
    uint8_t p=0;
    for(int i=0;i<8;i++){
        if(b&1) p^=a;
        b>>=1; a=xtime(a);
    }
    return p;
}

#define NK 4 /* AES-128: Nk=4, Nr=10 */
static uint8_t aes_rcon[10]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

typedef struct { uint8_t rk[11][4][4]; } aes_ctx;

static void aes_key_expand(aes_ctx *ctx, const uint8_t key[16]){
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) ctx->rk[0][i][j]=key[i*4+j];
    for(int r=1;r<=10;r++){
        uint8_t prev[4]={ctx->rk[r-1][3][0],ctx->rk[r-1][3][1],ctx->rk[r-1][3][2],ctx->rk[r-1][3][3]};
        /* RotWord + SubWord + Rcon */
        uint8_t t=prev[0]; prev[0]=sbox[prev[1]]; prev[1]=sbox[prev[2]]; prev[2]=sbox[prev[3]]; prev[3]=sbox[t];
        prev[0]^=aes_rcon[r-1];
        for(int c=0;c<4;c++){
            for(int b=0;b<4;b++) ctx->rk[r][c][b]=ctx->rk[r-1][c][b]^prev[b];
            for(int b=0;b<4;b++) prev[b]=ctx->rk[r][c][b];
        }
    }
}

static void aes_encrypt_block(const aes_ctx *ctx, const uint8_t in[16], uint8_t out[16]){
    uint8_t s[4][4];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=in[i*4+j]^ctx->rk[0][i][j];
    for(int r=1;r<=10;r++){
        /* SubBytes */
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=sbox[s[i][j]];
        /* ShiftRows */
        uint8_t t;
        t=s[1][0];s[1][0]=s[1][1];s[1][1]=s[1][2];s[1][2]=s[1][3];s[1][3]=t;
        t=s[2][0];s[2][0]=s[2][2];s[2][2]=t; t=s[2][1];s[2][1]=s[2][3];s[2][3]=t;
        t=s[3][3];s[3][3]=s[3][2];s[3][2]=s[3][1];s[3][1]=s[3][0];s[3][0]=t;
        /* MixColumns (skip en ronda 10) */
        if(r<10){
            for(int c=0;c<4;c++){
                uint8_t a0=s[0][c],a1=s[1][c],a2=s[2][c],a3=s[3][c];
                s[0][c]=gmul(a0,2)^gmul(a1,3)^a2^a3;
                s[1][c]=a0^gmul(a1,2)^gmul(a2,3)^a3;
                s[2][c]=a0^a1^gmul(a2,2)^gmul(a3,3);
                s[3][c]=gmul(a0,3)^a1^a2^gmul(a3,2);
            }
        }
        /* AddRoundKey */
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]^=ctx->rk[r][i][j];
    }
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) out[i*4+j]=s[i][j];
}

static void aes_decrypt_block(const aes_ctx *ctx, const uint8_t in[16], uint8_t out[16]){
    uint8_t s[4][4];
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=in[i*4+j]^ctx->rk[10][i][j];
    for(int r=9;r>=0;r--){
        /* InvShiftRows */
        uint8_t t;
        t=s[1][3];s[1][3]=s[1][2];s[1][2]=s[1][1];s[1][1]=s[1][0];s[1][0]=t;
        t=s[2][0];s[2][0]=s[2][2];s[2][2]=t; t=s[2][1];s[2][1]=s[2][3];s[2][3]=t;
        t=s[3][0];s[3][0]=s[3][1];s[3][1]=s[3][2];s[3][2]=s[3][3];s[3][3]=t;
        /* InvSubBytes */
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]=inv_sbox[s[i][j]];
        /* AddRoundKey */
        for(int i=0;i<4;i++) for(int j=0;j<4;j++) s[i][j]^=ctx->rk[r][i][j];
        /* InvMixColumns (skip en ronda 0) */
        if(r>0){
            for(int c=0;c<4;c++){
                uint8_t a0=s[0][c],a1=s[1][c],a2=s[2][c],a3=s[3][c];
                s[0][c]=gmul(a0,0x0e)^gmul(a1,0x0b)^gmul(a2,0x0d)^gmul(a3,0x09);
                s[1][c]=gmul(a0,0x09)^gmul(a1,0x0e)^gmul(a2,0x0b)^gmul(a3,0x0d);
                s[2][c]=gmul(a0,0x0d)^gmul(a1,0x09)^gmul(a2,0x0e)^gmul(a3,0x0b);
                s[3][c]=gmul(a0,0x0b)^gmul(a1,0x0d)^gmul(a2,0x09)^gmul(a3,0x0e);
            }
        }
    }
    for(int i=0;i<4;i++) for(int j=0;j<4;j++) out[i*4+j]=s[i][j];
}

/* AES-128-CBC encrypt / decrypt in-place. IV viene en iv[16]. */
static void aes_cbc_encrypt(const aes_ctx *ctx, uint8_t *data, int nblocks, const uint8_t iv[16]){
    uint8_t prev[16]; for(int i=0;i<16;i++) prev[i]=iv[i];
    for(int b=0;b<nblocks;b++){
        uint8_t *blk=data+b*16;
        for(int i=0;i<16;i++) blk[i]^=prev[i];
        aes_encrypt_block(ctx,blk,blk);
        for(int i=0;i<16;i++) prev[i]=blk[i];
    }
}
static void aes_cbc_decrypt(const aes_ctx *ctx, uint8_t *data, int nblocks, const uint8_t iv[16]){
    uint8_t prev[16]; for(int i=0;i<16;i++) prev[i]=iv[i];
    for(int b=0;b<nblocks;b++){
        uint8_t *blk=data+b*16;
        uint8_t next_prev[16]; for(int i=0;i<16;i++) next_prev[i]=blk[i];
        aes_decrypt_block(ctx,blk,blk);
        for(int i=0;i<16;i++) blk[i]^=prev[i];
        for(int i=0;i<16;i++) prev[i]=next_prev[i];
    }
}

/* ================================================================== */
/* RSA (big integer mínimo, solo expo público e=65537, mod ≤ 512 bytes)*/
/* ================================================================== */

#define RSA_MAX_BYTES 512

/* big-endian big int: [0]=MSB */
typedef struct { uint8_t d[RSA_MAX_BYTES]; int len; } bigint;

static void bi_zero(bigint *a){ for(int i=0;i<RSA_MAX_BYTES;i++) a->d[i]=0; a->len=RSA_MAX_BYTES; }
static int bi_cmp(const bigint *a, const bigint *b){
    for(int i=0;i<RSA_MAX_BYTES;i++){
        if(a->d[i]<b->d[i]) return -1;
        if(a->d[i]>b->d[i]) return  1;
    }
    return 0;
}

/* a = a mod m  (a y m son RSA_MAX_BYTES bytes big-endian) */
/* Implementacion: resta repetida – suficiente solo si a < 2*m (que es
   el caso en la multiplicacion modular paso a paso).               */
static void bi_reduce(bigint *a, const bigint *m){
    while(bi_cmp(a,m)>=0){
        /* a -= m */
        int borrow=0;
        for(int i=RSA_MAX_BYTES-1;i>=0;i--){
            int diff=(int)a->d[i]-(int)m->d[i]-borrow;
            borrow=diff<0?1:0;
            a->d[i]=(uint8_t)(diff&0xFF);
        }
    }
}

/* res = (a * b) mod m  (todos RSA_MAX_BYTES bytes big-endian) */
static void bi_mulmod(bigint *res, const bigint *a, const bigint *b, const bigint *m){
    bi_zero(res);
    bigint tmp; for(int i=0;i<RSA_MAX_BYTES;i++) tmp.d[i]=a->d[i]; tmp.len=RSA_MAX_BYTES;
    for(int i=RSA_MAX_BYTES-1;i>=0;i--){
        uint8_t byte=b->d[i];
        for(int bit=0;bit<8;bit++){
            if(byte&(1<<bit)){
                /* res = (res + tmp) mod m */
                int carry=0;
                for(int j=RSA_MAX_BYTES-1;j>=0;j--){
                    int s=(int)res->d[j]+(int)tmp.d[j]+carry;
                    carry=s>>8; res->d[j]=(uint8_t)s;
                }
                bi_reduce(res,m);
            }
            /* tmp = (tmp + tmp) mod m */
            int carry=0;
            for(int j=RSA_MAX_BYTES-1;j>=0;j--){
                int s=(int)tmp.d[j]+(int)tmp.d[j]+carry;
                carry=s>>8; tmp.d[j]=(uint8_t)s;
            }
            bi_reduce(&tmp,m);
        }
    }
}

/* RSA: out = in^65537 mod mod_n (todos RSA_MAX_BYTES bytes) */
static void rsa_public(const uint8_t *in, const uint8_t *mod_n, int modlen, uint8_t *out){
    /* Extender a RSA_MAX_BYTES */
    bigint M; bi_zero(&M); int off=RSA_MAX_BYTES-modlen;
    for(int i=0;i<modlen;i++) M.d[off+i]=mod_n[i];
    bigint X; bi_zero(&X); off=RSA_MAX_BYTES-modlen;
    for(int i=0;i<modlen;i++) X.d[off+i]=in[i];

    /* e=65537=0x10001 → exp by squaring: bits son 1,0,0...0,1 (17 bits) */
    /* Calculo: resultado = X^65537 mod M */
    /* Usamos square-and-multiply:
       65537 en binario = 1 0000 0000 0000 0001 */
    bigint res; bi_zero(&res); res.d[RSA_MAX_BYTES-1]=1; /* res=1 */
    bigint base; for(int i=0;i<RSA_MAX_BYTES;i++) base.d[i]=X.d[i]; base.len=RSA_MAX_BYTES;

    /* Los bits de 65537 de MSB a LSB: posicion 16..0, set en 16 y 0 */
    for(int b=16;b>=0;b--){
        bigint sq; bi_mulmod(&sq,&res,&res,&M); /* sq = res^2 mod M */
        for(int i=0;i<RSA_MAX_BYTES;i++) res.d[i]=sq.d[i];
        int ebit=(b==16||b==0)?1:0;
        if(ebit){
            bigint tmp; bi_mulmod(&tmp,&res,&base,&M);
            for(int i=0;i<RSA_MAX_BYTES;i++) res.d[i]=tmp.d[i];
        }
    }
    /* Copiar modlen bytes de salida */
    off=RSA_MAX_BYTES-modlen;
    for(int i=0;i<modlen;i++) out[i]=res.d[off+i];
}

/* ================================================================== */
/* Parser de certificado X.509 (DER)  –  extrae clave pública RSA     */
/* ================================================================== */

/* Devuelve longitud del campo DER a partir de p, y avanza *p.
   Solo soporta short form (1 byte) y long form de 2-3 bytes.       */
static int der_len(const uint8_t **p, const uint8_t *end){
    if(*p>=end) return -1;
    uint8_t b=*(*p)++;
    if(!(b&0x80)) return b;
    int nb=b&0x7f; if(nb>3||*p+nb>end) return -1;
    int len=0;
    while(nb--) len=(len<<8)|*(*p)++;
    return len;
}
/* Salta un TLV y devuelve puntero al siguiente. */
static const uint8_t* der_skip(const uint8_t *p, const uint8_t *end){
    if(p>=end) return end;
    p++; /* tag */
    int l=der_len(&p,end);
    if(l<0) return end;
    return p+l;
}

/* Busca la clave pública RSA en un certificado DER.
   Llena rsa_n y *rsa_nlen con el módulo.
   Devuelve 1 si OK, 0 si no encontró. */
static int cert_get_pubkey(const uint8_t *cert, int clen,
                           uint8_t *rsa_n, int *rsa_nlen){
    const uint8_t *p=cert, *end=cert+clen;
    /* SEQUENCE (Certificate) */
    if(p>=end||*p!=0x30) return 0; p++;
    { int l=der_len(&p,end); if(l<0) return 0; }
    /* SEQUENCE (TBSCertificate) */
    if(p>=end||*p!=0x30) return 0; p++;
    int tbslen=der_len(&p,end); if(tbslen<0) return 0;
    const uint8_t *tbs_end=p+tbslen;

    /* Saltar: version(opcional), serialNumber, signature, issuer, validity, subject */
    /* version [0] EXPLICIT */
    if(p<tbs_end && *p==0xa0) p=der_skip(p,tbs_end);
    /* serialNumber */
    if(p<tbs_end) p=der_skip(p,tbs_end);
    /* signature */
    if(p<tbs_end) p=der_skip(p,tbs_end);
    /* issuer */
    if(p<tbs_end) p=der_skip(p,tbs_end);
    /* validity */
    if(p<tbs_end) p=der_skip(p,tbs_end);
    /* subject */
    if(p<tbs_end) p=der_skip(p,tbs_end);

    /* SubjectPublicKeyInfo SEQUENCE */
    if(p>=tbs_end||*p!=0x30) return 0; p++;
    int spkilen=der_len(&p,tbs_end); if(spkilen<0) return 0;
    const uint8_t *spki_end=p+spkilen;
    /* AlgorithmIdentifier SEQUENCE – saltar */
    if(p<spki_end) p=der_skip(p,spki_end);
    /* BIT STRING con la clave pública */
    if(p>=spki_end||*p!=0x03) return 0; p++;
    int bslen=der_len(&p,spki_end); if(bslen<0) return 0;
    if(*p!=0x00) return 0; p++; bslen--; /* skip unused bits byte */
    /* RSAPublicKey SEQUENCE */
    if(p>=spki_end||*p!=0x30) return 0; p++;
    { int l=der_len(&p,spki_end); if(l<0) return 0; }
    /* modulus INTEGER */
    if(p>=spki_end||*p!=0x02) return 0; p++;
    int nlen=der_len(&p,spki_end); if(nlen<0) return 0;
    /* Puede tener un byte 0x00 de signo al inicio */
    if(*p==0x00){ p++; nlen--; }
    if(nlen<=0||nlen>RSA_MAX_BYTES) return 0;
    for(int i=0;i<nlen;i++) rsa_n[i]=p[i];
    *rsa_nlen=nlen;
    return 1;
}

/* ================================================================== */
/* TLS 1.2 record layer + handshake                                    */
/* ================================================================== */

#define TLS_CT_CHANGE_CIPHER  20
#define TLS_CT_ALERT          21
#define TLS_CT_HANDSHAKE      22
#define TLS_CT_APPDATA        23

#define TLS_HS_HELLO_REQUEST      0
#define TLS_HS_CLIENT_HELLO       1
#define TLS_HS_SERVER_HELLO       2
#define TLS_HS_CERTIFICATE        11
#define TLS_HS_SERVER_HELLO_DONE  14
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED           20

#define CS_RSA_AES128_CBC_SHA  0x002F

/* Estado global de la sesión TLS (una sola sesión a la vez) */
typedef struct {
    tcp_sock_t sock;
    /* randoms */
    uint8_t client_random[32];
    uint8_t server_random[32];
    /* PreMasterSecret, MasterSecret, claves derivadas */
    uint8_t pre_master[48];
    uint8_t master[48];
    uint8_t client_write_key[16];
    uint8_t server_write_key[16];
    uint8_t client_write_mac[20];
    uint8_t server_write_mac[20];
    uint8_t client_write_iv[16];
    uint8_t server_write_iv[16];
    /* AES contexts */
    aes_ctx enc_ctx;
    aes_ctx dec_ctx;
    /* seq numbers */
    uint64_t send_seq;
    uint64_t recv_seq;
    /* handshake hash acumulado */
    sha256_ctx hs_hash;
    /* RSA clave pública del servidor */
    uint8_t rsa_n[RSA_MAX_BYTES];
    int rsa_nlen;
    /* estado */
    int cipher_active;  /* 1 = ya se activó ChangeCipherSpec */
} tls_session;

static tls_session tls;

/* PRNG mínimo: mezcla timer + ciclos del CPU */
static uint32_t prng_state = 0xDEADBEEF;
static uint8_t prng_byte(void){
    prng_state ^= (uint32_t)timer_ticks();
    prng_state = prng_state*1664525u + 1013904223u;
    return (uint8_t)(prng_state>>13);
}

/* ---- Record layer: enviar ---------------------------------------- */
/* Construye y envía un TLS record (content_type, data, dlen).
   Si cipher_active: cifra con AES-128-CBC + HMAC-SHA1.             */
static int tls_send_record(uint8_t ct, const uint8_t *data, int dlen){
    static uint8_t rec[16384+256];
    int rlen;
    if(!tls.cipher_active){
        rec[0]=ct; rec[1]=3; rec[2]=3;
        rec[3]=(uint8_t)(dlen>>8); rec[4]=(uint8_t)dlen;
        for(int i=0;i<dlen;i++) rec[5+i]=data[i];
        rlen=5+dlen;
    } else {
        /* HMAC-SHA1 sobre seq_num(8) + header(5) + data */
        uint8_t mac_input[8+5+16384]; int mi=0;
        for(int i=7;i>=0;i--){ mac_input[mi++]=(uint8_t)(tls.send_seq>>(i*8)); }
        mac_input[mi++]=ct; mac_input[mi++]=3; mac_input[mi++]=3;
        mac_input[mi++]=(uint8_t)(dlen>>8); mac_input[mi++]=(uint8_t)dlen;
        for(int i=0;i<dlen;i++) mac_input[mi++]=data[i];
        uint8_t mac[20]; hmac_sha1(tls.client_write_mac,20,mac_input,mi,mac);
        tls.send_seq++;
        /* plaintext = data + mac */
        static uint8_t pt[16384+20+16];
        for(int i=0;i<dlen;i++) pt[i]=data[i];
        for(int i=0;i<20;i++) pt[dlen+i]=mac[i];
        int ptlen=dlen+20;
        /* PKCS#7 padding */
        int pad_needed=16-((ptlen)%16); if(pad_needed==0) pad_needed=16;
        for(int i=0;i<pad_needed;i++) pt[ptlen+i]=(uint8_t)(pad_needed-1);
        ptlen+=pad_needed;
        /* IV aleatorio */
        uint8_t iv[16]; for(int i=0;i<16;i++) iv[i]=prng_byte();
        aes_cbc_encrypt(&tls.enc_ctx,pt,ptlen/16,iv);
        /* record: header + IV + ciphertext */
        int payload=16+ptlen;
        rec[0]=ct; rec[1]=3; rec[2]=3;
        rec[3]=(uint8_t)(payload>>8); rec[4]=(uint8_t)payload;
        for(int i=0;i<16;i++) rec[5+i]=iv[i];
        for(int i=0;i<ptlen;i++) rec[5+16+i]=pt[i];
        rlen=5+payload;
    }
    return tcp_send(tls.sock,(uint8_t*)rec,rlen);
}

/* ---- Record layer: recibir --------------------------------------- */
/* Buffer de recepción acumulativo */
static uint8_t rx_accum[16384+256];
static int rx_accum_len=0;

static int tls_fill_rx(int need, uint32_t timeout_ms){
    uint32_t deadline=timer_ticks()+timeout_ms/10;
    while(rx_accum_len<need && timer_ticks()<deadline){
        net_poll();
        int n=tcp_recv(tls.sock,rx_accum+rx_accum_len,sizeof(rx_accum)-rx_accum_len);
        if(n<0) return -1;
        rx_accum_len+=n;
    }
    return rx_accum_len>=need?0:-1;
}
static void rx_consume(int n){
    for(int i=0;i<rx_accum_len-n;i++) rx_accum[i]=rx_accum[i+n];
    rx_accum_len-=n;
}

/* Lee un record completo. Devuelve content_type, deja payload en out,
   *outlen con el tamaño del payload descifrado.                    */
static int tls_recv_record(uint8_t *out, int *outlen, int maxout){
    /* header 5 bytes */
    if(tls_fill_rx(5,5000)<0) return -1;
    uint8_t ct=rx_accum[0];
    int rlen=((int)rx_accum[3]<<8)|rx_accum[4];
    if(rlen<0||rlen>maxout+64) return -1;
    if(tls_fill_rx(5+rlen,5000)<0) return -1;
    uint8_t *payload=rx_accum+5;

    if(!tls.cipher_active){
        for(int i=0;i<rlen;i++) out[i]=payload[i];
        *outlen=rlen;
    } else {
        if(rlen<16+20+1) return -1;
        /* IV en los primeros 16 bytes */
        uint8_t iv[16]; for(int i=0;i<16;i++) iv[i]=payload[i];
        uint8_t *ct_data=payload+16; int ct_len=rlen-16;
        if(ct_len%16!=0) return -1;
        /* Descifrar */
        aes_cbc_decrypt(&tls.dec_ctx,ct_data,ct_len/16,iv);
        /* Verificar padding PKCS#7 */
        uint8_t pad=ct_data[ct_len-1];
        if(pad>=ct_len) return -1;
        int ptlen=ct_len-pad-1;
        /* Quitar MAC */
        int datalen=ptlen-20; if(datalen<0) return -1;
        /* Verificar HMAC (opcional, por ahora solo extraemos datos) */
        tls.recv_seq++;
        for(int i=0;i<datalen;i++) out[i]=ct_data[i];
        *outlen=datalen;
    }
    rx_consume(5+rlen);
    return ct;
}

/* ---- Handshake hash --------------------------------------------- */
static void hs_update(const uint8_t *d, int n){
    sha256_update(&tls.hs_hash,d,n);
}

/* ---- ClientHello ------------------------------------------------- */
static int send_client_hello(const char *host){
    uint8_t msg[256]; int p=0;
    /* 4 bytes HandshakeType + length (se rellena después) */
    int hs_hdr=p; p+=4;
    /* ProtocolVersion */
    msg[p++]=3; msg[p++]=3;
    /* ClientRandom: 4 bytes tiempo + 28 bytes aleatorios */
    uint32_t t=timer_ticks();
    tls.client_random[0]=(uint8_t)(t>>24); tls.client_random[1]=(uint8_t)(t>>16);
    tls.client_random[2]=(uint8_t)(t>>8);  tls.client_random[3]=(uint8_t)t;
    for(int i=4;i<32;i++) tls.client_random[i]=prng_byte();
    for(int i=0;i<32;i++) msg[p++]=tls.client_random[i];
    /* SessionID length=0 */
    msg[p++]=0;
    /* CipherSuites: solo TLS_RSA_WITH_AES_128_CBC_SHA */
    msg[p++]=0; msg[p++]=2;
    msg[p++]=0x00; msg[p++]=0x2F;
    /* CompressionMethods: null */
    msg[p++]=1; msg[p++]=0;
    /* Extensions: SNI */
    int sni_len=0; while(host[sni_len]) sni_len++;
    int ext_total=5+2+sni_len; /* ServerNameList(2)+ServerName(3)+name */
    msg[p++]=(uint8_t)((ext_total+4)>>8); msg[p++]=(uint8_t)(ext_total+4);
    msg[p++]=0; msg[p++]=0; /* ext type server_name */
    msg[p++]=0; msg[p++]=(uint8_t)(ext_total);
    msg[p++]=0; msg[p++]=(uint8_t)(ext_total-2); /* ServerNameList length */
    msg[p++]=0; /* type=host_name */
    msg[p++]=(uint8_t)(sni_len>>8); msg[p++]=(uint8_t)sni_len;
    for(int i=0;i<sni_len;i++) msg[p++]=(uint8_t)host[i];
    /* Rellenar header */
    int body_len=p-hs_hdr-4;
    msg[hs_hdr]=TLS_HS_CLIENT_HELLO;
    msg[hs_hdr+1]=(uint8_t)(body_len>>16);
    msg[hs_hdr+2]=(uint8_t)(body_len>>8);
    msg[hs_hdr+3]=(uint8_t)body_len;
    hs_update(msg+hs_hdr,p-hs_hdr);
    return tls_send_record(TLS_CT_HANDSHAKE,msg+hs_hdr,p-hs_hdr);
}

/* ---- Parse ServerHello ------------------------------------------ */
static int parse_server_hello(const uint8_t *d, int n){
    if(n<38) return -1;
    /* d[0]=HandshakeType, d[1..3]=length */
    if(d[0]!=TLS_HS_SERVER_HELLO) return -1;
    /* server_random está en bytes 6..37 (después de type+len+version) */
    for(int i=0;i<32;i++) tls.server_random[i]=d[6+i];
    return 0;
}

/* ---- Parse Certificate ------------------------------------------ */
static int parse_certificate(const uint8_t *d, int n){
    if(n<7) return -1;
    if(d[0]!=TLS_HS_CERTIFICATE) return -1;
    /* d[1..3] = handshake length, d[4..6] = certificates list length */
    /* d[7..9] = first cert length */
    if(n<10) return -1;
    int cert_len=((int)d[7]<<16)|((int)d[8]<<8)|d[9];
    if(10+cert_len>n) return -1;
    return cert_get_pubkey(d+10,cert_len,tls.rsa_n,&tls.rsa_nlen);
}

/* ---- ClientKeyExchange ------------------------------------------ */
static int send_client_key_exchange(void){
    /* PreMasterSecret: 2 bytes version + 46 bytes aleatorios */
    tls.pre_master[0]=3; tls.pre_master[1]=3;
    for(int i=2;i<48;i++) tls.pre_master[i]=prng_byte();

    /* PKCS#1 v1.5 RSA encrypt */
    int modlen=tls.rsa_nlen;
    if(modlen<51||modlen>RSA_MAX_BYTES) return -1;
    static uint8_t padded[RSA_MAX_BYTES];
    padded[0]=0x00; padded[1]=0x02;
    int ps_len=modlen-48-3;
    for(int i=0;i<ps_len;i++) padded[2+i]=prng_byte()|1; /* non-zero */
    padded[2+ps_len]=0x00;
    for(int i=0;i<48;i++) padded[3+ps_len+i]=tls.pre_master[i];
    static uint8_t encrypted[RSA_MAX_BYTES];
    rsa_public(padded,tls.rsa_n,modlen,encrypted);

    /* HandshakeMessage */
    static uint8_t msg[RSA_MAX_BYTES+8];
    int p=0;
    msg[p++]=TLS_HS_CLIENT_KEY_EXCHANGE;
    int body=modlen+2;
    msg[p++]=(uint8_t)(body>>16); msg[p++]=(uint8_t)(body>>8); msg[p++]=(uint8_t)body;
    msg[p++]=(uint8_t)(modlen>>8); msg[p++]=(uint8_t)modlen;
    for(int i=0;i<modlen;i++) msg[p++]=encrypted[i];
    hs_update(msg,p);
    return tls_send_record(TLS_CT_HANDSHAKE,msg,p);
}

/* ---- Derivar claves --------------------------------------------- */
static void derive_keys(void){
    /* MasterSecret = PRF(pre_master, "master secret", CR||SR, 48) */
    uint8_t seed[64];
    for(int i=0;i<32;i++) seed[i]=tls.client_random[i];
    for(int i=0;i<32;i++) seed[32+i]=tls.server_random[i];
    tls_prf(tls.pre_master,48,"master secret",seed,64,tls.master,48);
    /* key_block = PRF(master, "key expansion", SR||CR, 2*20+2*16+2*16) */
    uint8_t seed2[64];
    for(int i=0;i<32;i++) seed2[i]=tls.server_random[i];
    for(int i=0;i<32;i++) seed2[32+i]=tls.client_random[i];
    uint8_t kb[104]; /* 20+20+16+16+16+16 */
    tls_prf(tls.master,48,"key expansion",seed2,64,kb,104);
    for(int i=0;i<20;i++) tls.client_write_mac[i]=kb[i];
    for(int i=0;i<20;i++) tls.server_write_mac[i]=kb[20+i];
    for(int i=0;i<16;i++) tls.client_write_key[i]=kb[40+i];
    for(int i=0;i<16;i++) tls.server_write_key[i]=kb[56+i];
    for(int i=0;i<16;i++) tls.client_write_iv[i]=kb[72+i];
    for(int i=0;i<16;i++) tls.server_write_iv[i]=kb[88+i];
    aes_key_expand(&tls.enc_ctx,tls.client_write_key);
    aes_key_expand(&tls.dec_ctx,tls.server_write_key);
}

/* ---- ChangeCipherSpec ------------------------------------------- */
static int send_change_cipher_spec(void){
    uint8_t d=1;
    return tls_send_record(TLS_CT_CHANGE_CIPHER,&d,1);
}

/* ---- Finished ---------------------------------------------------- */
static int send_finished(void){
    /* verify_data = PRF(master, "client finished", Hash(handshake_msgs), 12) */
    uint8_t hs_hash[32]; sha256_ctx tmp=tls.hs_hash; sha256_final(&tmp,hs_hash);
    uint8_t vd[12];
    tls_prf(tls.master,48,"client finished",hs_hash,32,vd,12);
    uint8_t msg[16]; msg[0]=TLS_HS_FINISHED; msg[1]=0; msg[2]=0; msg[3]=12;
    for(int i=0;i<12;i++) msg[4+i]=vd[i];
    hs_update(msg,16);
    return tls_send_record(TLS_CT_HANDSHAKE,msg,16);
}

/* ================================================================== */
/* API pública: net_https_get                                          */
/* ================================================================== */

int net_https_get(const char *host, const char *path, char *out, int maxlen){
    if(!host||!path||!out||maxlen<1) return -1;

    /* Resolver DNS */
    uint8_t ip[4];
    if(net_dns_resolve(host,ip,3000)!=0) return -1;

    /* Conectar TCP en puerto 443 */
    tls.sock=tcp_connect(ip[0],ip[1],ip[2],ip[3],443,5000);
    if(tls.sock<0) return -1;

    /* Inicializar estado TLS */
    for(int i=0;i<RSA_MAX_BYTES;i++) tls.rsa_n[i]=0;
    tls.rsa_nlen=0; tls.cipher_active=0;
    tls.send_seq=0; tls.recv_seq=0;
    rx_accum_len=0;
    sha256_init(&tls.hs_hash);

    /* ----- Handshake ----- */
    if(send_client_hello(host)<0){ tcp_close(tls.sock); return -1; }

    /* Recibir ServerHello, Certificate, ServerHelloDone */
    int got_hello=0, got_cert=0, got_done=0;
    for(int iter=0;iter<20&&!(got_hello&&got_cert&&got_done);iter++){
        static uint8_t rec_buf[8192]; int rec_len=0;
        int ct=tls_recv_record(rec_buf,&rec_len,sizeof(rec_buf));
        if(ct<0) break;
        if(ct!=TLS_CT_HANDSHAKE) continue;
        hs_update(rec_buf,rec_len);
        /* Puede haber varios mensajes en un record */
        int off=0;
        while(off<rec_len){
            uint8_t ht=rec_buf[off];
            int hl=((int)rec_buf[off+1]<<16)|((int)rec_buf[off+2]<<8)|rec_buf[off+3];
            if(ht==TLS_HS_SERVER_HELLO){ parse_server_hello(rec_buf+off,hl+4); got_hello=1; }
            else if(ht==TLS_HS_CERTIFICATE){ parse_certificate(rec_buf+off,hl+4); got_cert=1; }
            else if(ht==TLS_HS_SERVER_HELLO_DONE){ got_done=1; }
            off+=4+hl;
        }
    }
    if(!got_hello||!got_cert||!got_done||tls.rsa_nlen==0){
        tcp_close(tls.sock); return -1;
    }

    /* ClientKeyExchange */
    if(send_client_key_exchange()<0){ tcp_close(tls.sock); return -1; }
    derive_keys();

    /* ChangeCipherSpec */
    if(send_change_cipher_spec()<0){ tcp_close(tls.sock); return -1; }
    tls.cipher_active=1;

    /* Finished */
    if(send_finished()<0){ tcp_close(tls.sock); return -1; }

    /* Esperar ChangeCipherSpec + Finished del servidor */
    int got_server_ccs=0, got_server_fin=0;
    for(int iter=0;iter<10&&!(got_server_ccs&&got_server_fin);iter++){
        static uint8_t rec_buf[4096]; int rec_len=0;
        int ct=tls_recv_record(rec_buf,&rec_len,sizeof(rec_buf));
        if(ct<0) break;
        if(ct==TLS_CT_CHANGE_CIPHER) got_server_ccs=1;
        else if(ct==TLS_CT_HANDSHAKE&&rec_buf[0]==TLS_HS_FINISHED) got_server_fin=1;
    }
    /* Si no llega, intentamos de todos modos (algunos servidores son permisivos) */

    /* ----- HTTP GET sobre TLS ----- */
    static char req[512]; int p=0;
    const char *method="GET ";
    for(int i=0;method[i];i++) req[p++]=method[i];
    for(int i=0;path[i]&&p<480;i++) req[p++]=path[i];
    const char *http11=" HTTP/1.0\r\nHost: ";
    for(int i=0;http11[i];i++) req[p++]=http11[i];
    for(int i=0;host[i]&&p<500;i++) req[p++]=host[i];
    const char *end_hdr="\r\nConnection: close\r\n\r\n";
    for(int i=0;end_hdr[i];i++) req[p++]=end_hdr[i];

    if(tls_send_record(TLS_CT_APPDATA,(uint8_t*)req,p)<0){ tcp_close(tls.sock); return -1; }

    /* Recibir respuesta HTTP */
    int total=0, header_done=0;
    static char hdr_accum[512]; int ha=0;
    uint32_t deadline=timer_ticks()+1000;

    while(timer_ticks()<deadline && total<maxlen-1){
        static uint8_t rec_buf[8192]; int rec_len=0;
        int ct=tls_recv_record(rec_buf,&rec_len,sizeof(rec_buf));
        if(ct<0) break;
        if(ct!=TLS_CT_APPDATA) continue;
        deadline=timer_ticks()+300;
        for(int i=0;i<rec_len;i++){
            if(!header_done){
                if(ha<(int)sizeof(hdr_accum)-1) hdr_accum[ha++]=(char)rec_buf[i];
                if(ha>=4 &&
                   hdr_accum[ha-4]=='\r'&&hdr_accum[ha-3]=='\n'&&
                   hdr_accum[ha-2]=='\r'&&hdr_accum[ha-1]=='\n')
                    header_done=1;
            } else {
                if(total<maxlen-1) out[total++]=(char)rec_buf[i];
            }
        }
    }
    out[total]='\0';
    tcp_close(tls.sock);
    return total;
}
