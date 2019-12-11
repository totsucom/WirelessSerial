#include "basicio.h"
#include "main.h"

#define SERIAL_TX_BUF_SIZE      1000
#define RADIO_RX_SLOT_COUNT     5


typedef enum {
    CBID_EMPTY,
    CBID_WAITING,
    CBID_INSLOT,
    CBID_SENT,
    CBID_DISPOSE
} CBIDSTATUS;

uint8_t au8CbId[RADIO_TX_CBID_MASK + 1];

uint8_t i;
for (i = 0; i <= RADIO_TX_CBID_MASK; i++) {
    au8CbId[i] = CBID_EMPTY;
}

//最後にシリアルに送信した送信ID
static i16SerialTxCbId = -1;








typedef struct {
    int16_t i16CbId;                //無線送信ID -1:未使用スロットを表す
    uint32_t u32ReceiveTime;        //受信時のmillis()値
    uint8_t u8Length;               //データ長
    uint8_t au8Buf[RADIO_TX_SIZE];
} RADIORXSORTINGSLOT;

//重複受信回避用に送信IDを記憶
#define RADIO_CBID_MEMORY_COUNT     5
static uint8_t au8CbIdMemory[RADIO_CBID_MEMORY_COUNT];
static uint8_t cbIdMemoryCount;
static uint8_t cbIdMemoryStartIndex;
static uint8_t cbIdMemoryLastIndex;

//無線受信ソーティングスロット
static RADIORXSORTINGSLOT radioRxSlot[RADIO_RX_SLOT_COUNT];

//シリアル送信バッファ
static uint8_t au8SerialTxBuf[SERIAL_TX_BUF_SIZE];
static BYTEQUE serialTxQue;

//最後にシリアルに送信した送信ID
static i16SerialTxCbId;

//初期化
void serialTx_init() {
    cbIdMemoryCount = 0;
    i16SerialTxCbId = -1;
    
    uint8_t i;
    for(i = 0; i < RADIORXSORTINGSLOT; i++) {
        radioRxSlot[i].i16CbId = -1;
    }

    que_init(&serialTxQue, au8SerialTxBuf, SERIAL_TX_BUF_SIZE);
}

void cbidMemory_add(uint8_t u8CbId) {
    if (cbIdMemoryCount == RADIO_CBID_MEMORY_COUNT) {
        cbIdMemoryLastIndex = cbIdMemoryStartIndex;
        if (++cbIdMemoryStartIndex == RADIO_CBID_MEMORY_COUNT)
            cbIdMemoryStartIndex = 0;
        au8CbIdMemory[cbIdMemoryLastIndex] = u8CbId;
    }
    else if (cbIdMemoryCount > 0) {
        cbIdMemoryLastIndex++;
        au8CbIdMemory[cbIdMemoryLastIndex] = u8CbId;
        cbIdMemoryCount++;
    }
    else {
        cbIdMemoryStartIndex = 0;
        cbIdMemoryLastIndex = 0;
        cbIdMemoryCount = 1;
        au8CbIdMemory[0] = u8CbId;
    }
}

bool_t cbidMemory_inList(uint8_t u8CbId) {
    uint8_t i;
    for (i = 0; i < cbIdMemoryCount; i++) {
        if (au8CbIdMemory[i] == u8CbId)
            return TRUE;
    }
    return FALSE;
}

//スロットオーバーフローのときFALSEを返す
bool_t serialTx_add(uint8_t u8CbId, uint8_t *pData, uint8_t u8Len) {
    if (cbidMemory_inList(u8CbId))
        return TRUE;    //既に読んだデータ

    //送信IDを記憶
    cbidMemory_add(u8CbId);

    uint8_t i;
    for(i = 0; i < RADIORXSORTINGSLOT; i++) {
        if (radioRxSlot[i].i16CbId == -1) break;
    }
    if (i == RADIORXSORTINGSLOT)
        return FALSE;   //バッファに空きなし

    radioRxSlot[i].i16CbId = u8CbId;
    radioRxSlot[i].u32ReceiveTime = millis();
    radioRxSlot[i].u8Length = u8Len;
    memcpy(radioRxSlot[i].au8Buf, pData, u8Len);


    recvId = u8CbId & RADIO_TX_CBID_MASK;

    if (i16SerialTxCbId == -1) {

        if (u8CbID & RADIO_TX_CBID_PREV_BIT) {
            //スロットに入れる
            //スロット内処理
        } else {
            //送信キューに入れる
            i16SerialTxCbId = recvId;
        }
    } else {

        nextId = (i16SerialTxCbId + 1) & RADIO_TX_CBID_MASK;

        if (nextId == recvId) {
            //送信キューに入れる
            i16SerialTxCbId = recvId;
        }
        else if (nextId > recvId) //実際のコードは大小比較に注意
            //データLOST
            //受信データは破棄
        }
        else { //(recvId が 大きい)
            //スロットに入れる
        }
    }




        if (u8CbID & RADIO_TX_CBID_PREV_BIT) {
            //スロットに入れる
        } else {
            //送信キューに入れる
        }
    }

    i16SerialTxCbId


}
