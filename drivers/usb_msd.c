/*
 * usb_msd.c  –  USB Mass Storage Class, Bulk-Only Transport (BBB)
 *
 * Soporta:
 *   - Detección automática de dispositivos Mass Storage (clase 0x08)
 *     dentro de los dispositivos ya enumerados por usb_uhci.
 *   - Comandos SCSI: INQUIRY, READ CAPACITY(10), READ(10).
 *   - Lectura de sectores de 512 bytes (base para FAT32).
 *   - Sin malloc: un único dispositivo activo a la vez.
 *
 * Protocolo BBB (USB Mass Storage Bulk-Only):
 *   Host → Device : CBW  (Command Block Wrapper, 31 bytes)
 *   Device → Host : DATA (opcional)
 *   Device → Host : CSW  (Command Status Wrapper, 13 bytes)
 */

#include "usb_msd.h"
#include "usb.h"
#include <stdint.h>

/* ---- CBW / CSW ---------------------------------------------------- */
#define CBW_SIGNATURE  0x43425355u   /* "USBC" little-endian */
#define CSW_SIGNATURE  0x53425355u   /* "USBS" little-endian */

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;         /* 0x80 = IN (device→host), 0x00 = OUT */
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;         /* 0 = OK */
} csw_t;

/* ---- Estado global ------------------------------------------------ */
static usb_device_t *msd_dev   = 0;
static uint8_t       msd_ep_in = 0;
static uint8_t       msd_ep_out= 0;
static uint32_t      msd_tag   = 1;
static int           msd_ready = 0;
static uint32_t      msd_sectors= 0;  /* tamaño del dispositivo en sectores */

/* ---- Helpers ------------------------------------------------------ */
static uint32_t u32be(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

/* Ejecuta un comando SCSI sobre BBB.
 * cdb      : bloque de comando SCSI (hasta 16 bytes)
 * cdb_len  : longitud del CDB
 * buf      : buffer de datos (puede ser NULL si data_len==0)
 * data_len : bytes a transferir (>0 = IN desde dispositivo)
 * Retorna 0 en éxito, -1 en error.
 */
static int msd_command(const uint8_t *cdb, int cdb_len,
                        void *buf, int data_len, int dir_in){
    if (!msd_dev) return -1;

    /* --- CBW --- */
    cbw_t cbw;
    cbw.dCBWSignature         = CBW_SIGNATURE;
    cbw.dCBWTag               = msd_tag++;
    cbw.dCBWDataTransferLength= (uint32_t)data_len;
    cbw.bmCBWFlags            = dir_in ? 0x80 : 0x00;
    cbw.bCBWLUN               = 0;
    cbw.bCBWCBLength          = (uint8_t)(cdb_len & 0x1F);
    for (int i=0;i<16;i++) cbw.CBWCB[i] = (i<cdb_len) ? cdb[i] : 0;

    if (usb_bulk_transfer(msd_dev, msd_ep_out, &cbw, 31, 0) < 0)
        return -1;

    /* --- DATA (opcional) --- */
    if (data_len > 0 && buf){
        int got = usb_bulk_transfer(msd_dev, msd_ep_in, buf, data_len, 1);
        if (got < 0) return -1;
    }

    /* --- CSW --- */
    csw_t csw;
    if (usb_bulk_transfer(msd_dev, msd_ep_in, &csw, 13, 1) < 0)
        return -1;
    if (csw.dCSWSignature != CSW_SIGNATURE) return -1;
    if (csw.bCSWStatus != 0) return -1;

    return 0;
}

/* ---- Detección de endpoints Mass Storage -------------------------- */
/* Lee el Configuration Descriptor para encontrar los endpoints Bulk IN/OUT
 * de la primera interfaz Mass Storage (clase 08, subclase 06, proto 50). */
static int find_msd_endpoints(usb_device_t *dev, uint8_t *ep_in, uint8_t *ep_out){
    uint8_t buf[255];
    /* GET_DESCRIPTOR Configuration (type=2, index=0) */
    if (usb_control_transfer(dev, 0x80, 0x06, (2<<8)|0, 0, 255, buf, 1) != 0)
        return -1;

    int total = (int)(buf[2] | (buf[3]<<8));
    if (total > 255) total = 255;

    int i = 0;
    int in_msd_iface = 0;
    *ep_in  = 0;
    *ep_out = 0;

    while (i < total){
        uint8_t len  = buf[i];
        uint8_t type = buf[i+1];
        if (len < 2) break;

        if (type == 0x04){ /* Interface Descriptor */
            uint8_t cls  = buf[i+5];
            uint8_t sub  = buf[i+6];
            uint8_t proto= buf[i+7];
            in_msd_iface = (cls==0x08 && sub==0x06 && proto==0x50) ? 1 : 0;
        } else if (type == 0x05 && in_msd_iface){ /* Endpoint Descriptor */
            uint8_t addr  = buf[i+2];
            uint8_t attr  = buf[i+3];
            if ((attr & 0x03) == 0x02){ /* Bulk */
                if (addr & 0x80)
                    *ep_in  = addr & 0x0F;
                else
                    *ep_out = addr & 0x0F;
            }
        }
        i += len;
    }
    return (*ep_in && *ep_out) ? 0 : -1;
}

/* ---- API pública -------------------------------------------------- */

void usb_msd_init(void){
    msd_dev   = 0;
    msd_ready = 0;
    msd_sectors = 0;

    int count = usb_device_count();
    for (int i = 0; i < count; i++){
        usb_device_t *dev = usb_get_device(i);
        if (!dev || !dev->valid) continue;
        /* Clase 0x08 = Mass Storage.
         * Clase 0x00 = Composite (la clase real está en la interfaz —
         * find_msd_endpoints verifica esto internamente). */
        if (dev->dev_class != 0x08 && dev->dev_class != 0x00) continue;

        uint8_t ep_in=0, ep_out=0;
        if (find_msd_endpoints(dev, &ep_in, &ep_out) != 0) continue;

        msd_dev    = dev;
        msd_ep_in  = ep_in;
        msd_ep_out = ep_out;

        /* INQUIRY */
        uint8_t cdb_inq[6] = {0x12,0,0,0,36,0};
        uint8_t inq[36];
        if (msd_command(cdb_inq,6,inq,36,1) != 0) { msd_dev=0; continue; }

        /* READ CAPACITY(10) */
        uint8_t cdb_rc[10] = {0x25,0,0,0,0,0,0,0,0,0};
        uint8_t rc[8];
        if (msd_command(cdb_rc,10,rc,8,1) != 0) { msd_dev=0; continue; }

        msd_sectors = u32be(rc) + 1;  /* último LBA + 1 */
        /* sector size = u32be(rc+4), debe ser 512 */

        msd_ready = 1;
        break;
    }
}

int usb_msd_present(void){ return msd_ready; }
uint32_t usb_msd_sector_count(void){ return msd_sectors; }

/* Lee 'count' sectores de 512 bytes desde LBA 'lba' hacia 'buf'.
 * Retorna 0 en éxito, -1 en error. */
int usb_msd_read_sectors(uint32_t lba, uint32_t count, void *buf){
    if (!msd_ready) return -1;
    /* READ(10): opcode 0x28 */
    uint8_t cdb[10];
    cdb[0] = 0x28;                       /* READ(10)        */
    cdb[1] = 0;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >>  8);
    cdb[5] = (uint8_t)(lba      );
    cdb[6] = 0;
    cdb[7] = (uint8_t)(count >> 8);
    cdb[8] = (uint8_t)(count     );
    cdb[9] = 0;
    return msd_command(cdb, 10, buf, (int)(count * 512), 1);
}
