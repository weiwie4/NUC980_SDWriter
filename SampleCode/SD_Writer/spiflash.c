/******************************************************************************
 * @file     spiflash.c
 * @brief    SPI flash driver
 *
 * @copyright (C) 2018 Nuvoton Technology Corp. All rights reserved.
*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nuc980.h"
#include "sys.h"
#include "fmi.h"
#include "qspi.h"
#include "etimer.h"
#include "gpio.h"

#define TIMEOUT  (1000000)   /* unit of timeout is micro second */
#define QSPI_FLASH_PORT    QSPI0

unsigned int volatile u32PowerOn;

extern void SetTimer(unsigned int count);
extern void DelayMicrosecond(unsigned int count);
extern INFO_T info;

typedef struct {
    char PID;
    int (*SpiFlashWrite)(UINT32 address, UINT32 len, UCHAR *data);
} spiflash_t;


UINT8 spiStatusRead(void);
int wbSpiWrite(UINT32 addr, UINT32 len, UINT8 *buf);
int sstSpiWrite(UINT32 addr, UINT32 len, UINT8 *buf);
int spiEraseAll(void);
int spiCheckBusy(void);
int spiEraseSector(UINT32 addr, UINT32 secCount);
int spiWrite(UINT32 addr, UINT32 len, UINT8 *buf);
int spiEnable4ByteAddressMode(void);
int spiDisable4ByteAddressMode(void);
int spiReadFast(UINT32 addr, UINT32 len, UINT8 *buf);

volatile unsigned char Enable4ByteFlag=0;

spiflash_t spiflash[]= {
    {0xEF, wbSpiWrite},
    {0xBF, sstSpiWrite},
    {0}
};

/*****************************************/

INT32 volatile _spi_type = -1;


int spiActive(void)
{
    SetTimer(TIMEOUT);
    // wait tx finish
    while(QSPI_IS_BUSY(QSPI_FLASH_PORT)) {
        if (inpw(REG_ETMR0_ISR) & 0x1) {
            outpw(REG_ETMR0_ISR, 0x1);
            return Fail;
        }
    }
    return Successful;
}

int spiTxLen(int count, int bitLen)
{
    return Successful;
}

int spiCheckBusy(void)
{
    uint8_t volatile ReturnValue;

    do {
        ReturnValue = spiStatusRead();
        ReturnValue = ReturnValue & 1;
    } while(ReturnValue!=0); // check the BUSY bit

    return Successful;
}

/*
    addr: memory address
    len: byte count
    buf: buffer to put the read back data
*/
int spiRead(UINT32 addr, UINT32 len, UINT8 *buf)
{
    int volatile i;

    //printf("spiRead Enable4ByteFlag %d _spi_type =%d,  addr =0x%x(%d)\n",Enable4ByteFlag, _spi_type, addr, addr/16/1024/1024);
    if(Enable4ByteFlag==1)  spiEnable4ByteAddressMode();
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x03, Read data
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x3);

    spiActive();

    if(Enable4ByteFlag==1) { // send 32-bit start address
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>24) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>16) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>8)  & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, addr       & 0xFF);
    } else { // send 24-bit start address
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>16) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>8)  & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, addr       & 0xFF);
    }

    spiActive();
    // clear RX buffer
    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    // data
    for (i=0; i<len; i++) {
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
        spiActive();
        buf[i] = QSPI_READ_RX(QSPI_FLASH_PORT);
    }

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    if(Enable4ByteFlag==1)  spiDisable4ByteAddressMode();

    return Successful;
}

void D2D3_SwitchToNormalMode(void)
{
    outpw(REG_SYS_GPD_MFPL, (inpw(REG_SYS_GPD_MFPL) & ~0xFF000000));
    GPIO_SetMode(PA, BIT4, GPIO_MODE_OUTPUT);
    GPIO_SetMode(PA, BIT5, GPIO_MODE_OUTPUT);
    PD6 = 1;
    PD7 = 1;
}

void D2D3_SwitchToQuadMode(void)
{
    outpw(REG_SYS_GPD_MFPL, (inpw(REG_SYS_GPD_MFPL) & ~0xFF000000) | 0x11000000);
}

uint8_t SpiFlash_ReadStatusReg(void)
{
    uint8_t volatile u8Val;

    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x05, Read status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x05);

    // read status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    // skip first rx data
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);

    return u8Val;
}

uint8_t SpiFlash_ReadStatusReg2(void)
{
    uint8_t volatile u8Val;

    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x35, Read status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x35);

    // read status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    // skip first rx data
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);

    return u8Val;
}

void SpiFlash_WriteStatusReg(uint8_t u8Value1, uint8_t u8Value2)
{
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x06, Write enable
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x06);

    // wait tx finish
    while(QSPI_IS_BUSY(QSPI_FLASH_PORT));

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    ///////////////////////////////////////

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x01, Write status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x01);

    // write status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, u8Value1);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, u8Value2);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);
}

int spiEnable4ByteAddressMode(void)
{
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // read status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0xB7);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    return Successful;
}

int spiDisable4ByteAddressMode(void)
{
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // read status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0xE9);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    return Successful;
}

int spiReadFast(UINT32 addr, UINT32 len, UINT8 *buf)
{
    int volatile i;

    if(Enable4ByteFlag==1) {
        spiEnable4ByteAddressMode();
    }

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // Command: 0xEB, Fast Read quad data
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0xEB);
    while(QSPI_IS_BUSY(QSPI_FLASH_PORT));

    // enable QSPI quad IO mode and set direction to input
    D2D3_SwitchToQuadMode();
    QSPI_ENABLE_QUAD_OUTPUT_MODE(QSPI_FLASH_PORT);

    if(Enable4ByteFlag==1) {
        // send 32-bit start address
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>24) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>16) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>8)  & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, addr       & 0xFF);
    } else {
        // send 24-bit start address
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>16) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>8)  & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, addr       & 0xFF);
    }

    // dummy byte
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    while(QSPI_IS_BUSY(QSPI_FLASH_PORT));
    QSPI_ENABLE_QUAD_INPUT_MODE(QSPI_FLASH_PORT);

    // clear RX buffer
    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    // read data
    for (i=0; i<len; i++) {
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
        while(QSPI_IS_BUSY(QSPI_FLASH_PORT));
        buf[i] = QSPI_READ_RX(QSPI_FLASH_PORT);
    }

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    if(Enable4ByteFlag==1) {
        QSPI_DISABLE_QUAD_MODE(QSPI_FLASH_PORT);
        D2D3_SwitchToNormalMode();
        spiDisable4ByteAddressMode();
    }

    return Successful;
}

int spiWriteEnable()
{
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x06, Write enable
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x06);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    return Successful;
}

int spiWriteDisable()
{
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x04, Write Disable
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x04);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    return Successful;
}

/*
    addr: memory address
    len: byte count
    buf: buffer with write data
*/
int spiWrite(UINT32 addr, UINT32 len, UINT8 *buf)
{
    int volatile status;
    //printf("spiWrite Enable4ByteFlag %d _spi_type =%d,  addr =0x%x(%d)\n",Enable4ByteFlag, _spi_type, addr, addr/16/1024/1024);
    if(Enable4ByteFlag==1)  spiEnable4ByteAddressMode();
    status=spiflash[_spi_type].SpiFlashWrite(addr, len, buf);
    if(Enable4ByteFlag==1)  spiDisable4ByteAddressMode();

    return status;
}

int spiEraseSector(UINT32 addr, UINT32 secCount)
{
    int volatile i;
    UINT32 StartAddress;

    if(Enable4ByteFlag==1)  spiEnable4ByteAddressMode();

    if ((addr % (64*1024)) != 0)
        return -1;

    for (i=0; i<secCount; i++) {
        spiWriteEnable();

        // /CS: active
        QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

        // send Command: 0xd8, Erase 64KB block
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0xd8);

        // wait tx finish
        spiActive();

        // send 24-bit start address
        StartAddress = addr + (i*64*1024);
        if(Enable4ByteFlag==1) {
            // send 32-bit start address
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>24) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>16) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>8)  & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, StartAddress       & 0xFF);
        } else {
            // send 24-bit start address
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>16) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>8)  & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, StartAddress       & 0xFF);
        }


        // wait tx finish
        spiActive();

        // /CS: de-active
        QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

        // check status
        spiCheckBusy();
    }

    if(Enable4ByteFlag==1)  spiDisable4ByteAddressMode();

    return Successful;
}

extern void SendAck(UINT32 status);
int spiEraseAll()
{
    unsigned int volatile count;

    // send Command: 0x06, Write enable
    spiWriteEnable();

    //////////////////////////////////////////

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0xC7, Chip Erase
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0xC7);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    //////////////////////////////////////////
    // check status
    ETIMER_Delay(0, 10);
    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x05, Read status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x05);

    spiActive();

    // get status
    count=0;
    while(1) {
        // read status
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

        // wait tx finish
        spiActive();

        if ((QSPI_READ_RX(QSPI_FLASH_PORT) & 0xff) != 0x01) break;
        (count)++;
        if (count > 95) count = 95;
        ETIMER_Delay(0, 100);
        SendAck(count);
        printf(" %2d\r",count);
    }
    printf("  \r");

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);
    ETIMER_Delay(0, 100);

    return Successful;
}


UINT16 spiReadID()
{
    UINT16 volatile id;
    int volatile i;

    uint8_t u8RxData[6], u8IDCnt = 0;

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x90, Read Manufacturer/Device ID
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x90);

    // send 24-bit '0', dummy
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    spiActive();

    // receive 16-bit
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    while(!QSPI_GET_RX_FIFO_EMPTY_FLAG(QSPI_FLASH_PORT))
        u8RxData[u8IDCnt++] = QSPI_READ_RX(QSPI_FLASH_PORT);

    id = (u8RxData[4]<<8) | u8RxData[5];
    info.SPI_ID = id;

    if (((id & 0xff00) >> 8) == 0xBF)
        _spi_type = 1;
    else
        _spi_type = 0;

    printf("SPI NOR ID=0x%08x  _spi_type =%d\n",id, _spi_type);
#if(1)
    if( (id & 0xffff) == 0xc218) Enable4ByteFlag=1;
    if( (id & 0xffff) == 0x1C18) Enable4ByteFlag=1;
    if( (id & 0xffff) == 0xEF18) Enable4ByteFlag=1; //Winbond 25q256fvfg
    if( (id & 0xffff) == 0xC818) Enable4ByteFlag=1; //GD 32MB
    if(Enable4ByteFlag ==1) {
        printf("Enable4ByteFlag  ID=0x%08x   _spi_type =%d\n",id, _spi_type);
    }
#endif

    if(id != 0xffff) {
        printf("SPI NOR ID=0x%08x  _spi_type =%d, %s\n",id, _spi_type, (info.SPI_uIsUserConfig==1)?"User Configure":"Auto Detect");
        if(info.SPI_uIsUserConfig == 0) {
            if(((id & 0xff00) >> 8) == 0x1C || ((id & 0xff00) >> 8) == 0xC2) { /* mxic */
                info.SPI_ReadStatusCmd = 0x05; // Read Status Register
                info.SPI_WriteStatusCmd = 0x1;// Write Status Register
                info.SPI_StatusValue = 0x40; // QE(Status Register[6]) Bit 6
                info.SPI_QuadReadCmd = 0x6b;
                info.SPI_dummybyte = 1;
            } else if((u8RxData[4]>>8 & 0xff) == 0x20) { /* micron ? */
                info.SPI_ReadStatusCmd = 0x05; // Read Status Register
                info.SPI_WriteStatusCmd = 0x1;// Write Status Register
                info.SPI_StatusValue = 0x40; // ?? QE(Status Register[6]) Bit 6
                info.SPI_QuadReadCmd = 0xeb;
                info.SPI_dummybyte = 6;
            } else { /* winbond or Unknown */
                info.SPI_ReadStatusCmd = 0x35; // Read Status Register2
                info.SPI_WriteStatusCmd = 0x31;// Write Status Register2
                info.SPI_StatusValue = (0x1 << 1); // QE(Status Register2[1]) Bit 1
                info.SPI_QuadReadCmd = 0xeb;//0x6b
                info.SPI_dummybyte = 3;//1
            }
        }
    }

    return id;
}

UINT8 spiStatusRead()
{
    uint8_t volatile u8Val;

    QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x05, Read status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x05);

    // wait tx finish
    spiActive();

    // read status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x00);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    // skip first rx data
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);
    u8Val = QSPI_READ_RX(QSPI_FLASH_PORT);

    return u8Val;
}

int spiStatusWrite(UINT8 data)
{
    // Write enable
    spiWriteEnable();

    ///////////////////////////////////////

    // /CS: active
    QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

    // send Command: 0x01, Write status register
    QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x01);

    // wait tx finish
    spiActive();

    // write status
    QSPI_WRITE_TX(QSPI_FLASH_PORT, data);

    // wait tx finish
    spiActive();

    // /CS: de-active
    QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

    return Successful;
}

BOOL volatile _usbd_bIsSPIInit = FALSE;
int spiInit()
{
    //int volatile tick;
    int ret = 1;
    if (_usbd_bIsSPIInit == FALSE) {
        /* Configure multi function pins to QSPI0 */
        outpw(REG_SYS_GPD_MFPL, (inpw(REG_SYS_GPD_MFPL) & ~0xFFFFFF00) | 0x11111100); // PD.2 ~ PD.7

        /* Configure QQSPI_FLASH_PORT as a master, MSB first, 8-bit transaction, QSPI Mode-0 timing, clock is 20MHz */
        QSPI_Open(QSPI_FLASH_PORT, QSPI_MASTER, QSPI_MODE_0, 8, 20000000);
        /* Disable auto SS function, control SS signal manually. */
        QSPI_DisableAutoSS(QSPI_FLASH_PORT);
        if (spiReadID() == 0xffff)
            return Fail;

        spiStatusWrite(0x00);   // clear block protect

				// check status
        spiCheckBusy();  //CWWeng 2018.11.14
				
        _usbd_bIsSPIInit = TRUE;
        ret = 0;
    }

    return ret;

} /* end spiInit */

int DelSpiSector(UINT32 start, UINT32 len)
{
    int i;
    for(i=0; i<len; i++) {
        spiEraseSector((start+i)*64*1024, 1);
        SendAck(((i+1)*100)/len);
    }
    return Successful;
}

int DelSpiImage(UINT32 imageNo)
{
    int i, count;
    unsigned int startOffset=0, length=0;
    unsigned char *pbuf;
    unsigned int *ptr;
    UCHAR _fmi_ucBuffer[64*1024];

    pbuf = (UINT8 *)((UINT32)_fmi_ucBuffer | 0x80000000);
    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);
    SendAck(10);
    spiRead( (SPI_HEAD_ADDR-1)*64*1024, 64*1024, (UINT8 *)pbuf);
    ptr = (unsigned int *)(pbuf + 63*1024);
    SendAck(30);
    if (((*(ptr+0)) == 0xAA554257) && ((*(ptr+3)) == 0x63594257)) {
        count = *(ptr+1);
        *(ptr+1) = count - 1;   // del one image

        /* pointer to image information */
        ptr += 4;
        for (i=0; i<count; i++) {
            if ((*(ptr) & 0xffff) == imageNo) {
                startOffset = *(ptr + 1);
                length = *(ptr + 3);
                break;
            }
            /* pointer to next image */
            ptr = ptr+8;
        }
        memcpy((char *)ptr, (char *)(ptr+8), (count-i-1)*32);
    }
    SendAck(40);
    spiEraseSector((SPI_HEAD_ADDR-1)*64*1024, 1);   /* erase sector 0 */

    /* send status */
    SendAck(50);

    spiWrite((SPI_HEAD_ADDR-1)*64*1024, 64*1024, pbuf);
    SendAck(80);
    // erase the sector
    {
        int tmpCnt=0;
        if (startOffset != 0) {
            tmpCnt = length / (64 * 1024);
            if ((length % (64 * 1024)) != 0)
                tmpCnt++;

            spiEraseSector(startOffset, tmpCnt);
        }
    }
    SendAck(100);
    return Successful;
}

int ChangeSpiImageType(UINT32 imageNo, UINT32 imageType)
{
    int i, count;
    unsigned char *pbuf;
    unsigned int *ptr;
    UCHAR _fmi_ucBuffer[64*1024];

    pbuf = (UINT8 *)((UINT32)_fmi_ucBuffer | 0x80000000);
    ptr = (unsigned int *)((UINT32)_fmi_ucBuffer | 0x80000000);

    spiRead( (SPI_HEAD_ADDR-1)*64*1024, 64*1024, (UINT8 *)pbuf);
    ptr = (unsigned int *)(pbuf + 63*1024);

    if (((*(ptr+0)) == 0xAA554257) && ((*(ptr+3)) == 0x63594257)) {
        count = *(ptr+1);

        /* pointer to image information */
        ptr += 4;
        for (i=0; i<count; i++) {
            if ((*ptr & 0xffff) == imageNo) {
                *ptr = ((imageType & 0xffff) << 16) | (imageNo & 0xffff);
                break;
            }
            /* pointer to next image */
            ptr = ptr+8;
        }
    }

    spiEraseSector((SPI_HEAD_ADDR-1)*64*1024, 1);   /* erase sector 0 */

    spiWrite((SPI_HEAD_ADDR-1)*64*1024, 64*1024, pbuf);

    return Successful;
}


/******************************************/
/* write function for different spi flash */
/******************************************/
int wbSpiWrite(UINT32 addr, UINT32 len, UINT8 *buf)
{
    int volatile count=0, page, i, idx = 0;
    UINT32 StartAddress;

    count = len / 256;
    if ((len % 256) != 0)
        count++;

    for (i=0; i<count; i++) {
        // check data len
        if (len >= 256) {
            page = 256;
            len = len - 256;
        } else
            page = len;

        spiWriteEnable();

        // /CS: active
        QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

        // send Command: 0x02, Page program (up to 256 bytes)
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x02);
        spiActive();

        // address
        StartAddress = (addr+(i*256));

        if(Enable4ByteFlag==1) {
            // send 32-bit start address
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>24) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>16) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>8)  & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, StartAddress       & 0xFF);
        } else {
            // send 24-bit start address
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>16) & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, (StartAddress>>8)  & 0xFF);
            QSPI_WRITE_TX(QSPI_FLASH_PORT, StartAddress       & 0xFF);
        }

        spiActive();

        // write data
        while (page > 0) {
            if(!QSPI_GET_TX_FIFO_FULL_FLAG(QSPI_FLASH_PORT)) {
                QSPI_WRITE_TX(QSPI_FLASH_PORT, buf[idx++]);
                page--;
            }
        }

        // wait tx finish
        spiActive();

        // /CS: de-active
        QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

        QSPI_ClearRxFIFO(QSPI_FLASH_PORT);

        // check status
        spiCheckBusy();
    }

    return Successful;
}

int sstSpiWrite(UINT32 addr, UINT32 len, UINT8 *buf)
{
    while (len > 0) {
        spiWriteEnable();

        // /CS: active
        QSPI_SET_SS_LOW(QSPI_FLASH_PORT);

        // send Command: 0x02, Page program (up to 256 bytes)
        QSPI_WRITE_TX(QSPI_FLASH_PORT, 0x02);

        // wait tx finish
        spiActive();

        // send 24-bit start address
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>16) & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, (addr>>8)  & 0xFF);
        QSPI_WRITE_TX(QSPI_FLASH_PORT, addr       & 0xFF);

        addr++;

        // wait tx finish
        spiActive();

        // write data
        QSPI_WRITE_TX(QSPI_FLASH_PORT, *buf++);

        // wait tx finish
        spiActive();

        // /CS: de-active
        QSPI_SET_SS_HIGH(QSPI_FLASH_PORT);

        // check status
        spiCheckBusy();

        len--;
    }

    return Successful;
}
