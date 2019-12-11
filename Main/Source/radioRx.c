#include "basicio.h"
#include "main.h"
#include "signal.h"
#include "radioRx.h"
#include "debug.h"

//参照するグローバル変数
extern bool_t g_bRxProtectMode;


typedef enum {
    RADIO_RX_SLOT_EMPTY,
    RADIO_RX_SLOT_NEW,
    RADIO_RX_SLOT_WAITING_PREV_PACKET
} E_RADIO_RX_SLOT_STATUS;

typedef struct {
    E_RADIO_RX_SLOT_STATUS  eStatus;
    uint8_t                 u8CbId;
    uint8_t                 au8Data[PACKET_LENGTH_MAX];
    uint8_t                 u8Length;
    uint32_t                tReceive;   //標準モード用
    bool_t                  bPrevData;  //標準モード用
} S_RADIO_RX_SLOT;

//受信スロットの実体
#define RADIO_RX_SLOT_SIZE  10
static S_RADIO_RX_SLOT m_arSlot[RADIO_RX_SLOT_SIZE];

//次にシリアルに書き出すCbId値
static int16_t m_i16SerialTxCbId;

//シリアルに書き出したCbId値（標準モード用）
static int16_t m_i16SentCbId;

//次に無線受信するであろうCbId値（保護モード用）
//static int16_t m_i16RxCbId;

//エラー（保護モード用）
static int16_t m_bHasError;


/* ローカル関数のプロトタイプ */
static void sendToSerial_PM();
static void sendToSerial_SM();
static S_RADIO_RX_SLOT *getEmpty();
static S_RADIO_RX_SLOT *findSlotByCbId(uint8_t cbId);
static S_RADIO_RX_SLOT *findTimeout();
static S_RADIO_RX_SLOT *getNextSend();
static int16_t findMinValueIndexInByteGroup(uint8_t *array, uint8_t len);


//受信スロットを初期化する
//相手から通信開始を受け取った
//次に受信するであろうCbId値を渡す。相手からの通信開始データのCbId値+1
void radioRx_init(uint8_t nextCbId) {
    uint8_t i;
    for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
        m_arSlot[i].eStatus = RADIO_RX_SLOT_EMPTY;
    }
    //m_i16RxCbId = nextCbId;
    m_i16SerialTxCbId = g_bRxProtectMode ? nextCbId : -1;
    m_i16SentCbId = -1;
    m_bHasError = FALSE;
}

//次にシリアルに書き出すCbId値
//保護モードで通信開始後のみ有効
uint8_t radioRx_getNextCbId() {
    return m_i16SerialTxCbId;
}

//受信したユーザーデータを渡す
//無線受信コールバックから呼び出される
void radioRx_add(uint8_t cbId, uint8_t dataType, uint8_t *pData, uint8_t len) {

    if (!g_bRxProtectMode) {
        debug_printf(DEBUG_INFO, "RECV ID:%02X PREV:%d %dB", cbId, (dataType & PACKET_DATATYPE_PREVDATA_BIT) ? 1 : 0, len);
    } else {
        debug_printf(DEBUG_INFO, "RECV ID:%02X %dB", cbId, len);
    }

    //空きスロットを取得する
    S_RADIO_RX_SLOT *pSlot = getEmpty();

    if (!pSlot) {
        //空きが無い。エラー
        m_bHasError = TRUE;

        //受信エラーを出力
        signal_rxError();

        debug_puts(DEBUG_ERROR, "RADIO RX SLOT FULL. DATA LOST");
/*
uint8_t i;
sb_clear();
sb_puts("CbId[]=");
for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
    sb_printf(" [%d]%d %X", i, m_arSlot[i].eStatus, m_arSlot[i].u8CbId);
}
debug_puts(DEBUG_INFO, sb_getBuffer());
*/

        //データは破棄
        return;
    }

    //スロットに保存
    pSlot->eStatus = RADIO_RX_SLOT_NEW;
    pSlot->u8CbId = cbId;
    memcpy(pSlot->au8Data, pData, len);
    pSlot->u8Length = len;
    pSlot->tReceive = millis();
    pSlot->bPrevData = g_bRxProtectMode ? FALSE : (dataType & PACKET_DATATYPE_PREVDATA_BIT);
}

//受信スロット内のデータを処理してシリアルに書き出す
void radioRx_sendToSerial() {
    if (g_bRxProtectMode) {
        sendToSerial_PM();
    } else {
        sendToSerial_SM();
    }
}

//受信スロット内のデータを処理してシリアルに書き出す
//保護モード
static void sendToSerial_PM() {

    //保護モードでエラーが発生すると処理を中断する
    if (m_bHasError) return;

    //次にシリアルに書き出すスロットを取得する
    S_RADIO_RX_SLOT *pSlot = findSlotByCbId(m_i16SerialTxCbId);

    if (pSlot) {
        //見つかった

        //シリアルに書き出す
        if (serial_write(pSlot->au8Data, pSlot->u8Length)) {
            //成功した

            //スロットを空にする
            pSlot->eStatus = RADIO_RX_SLOT_EMPTY;

            //次にシリアルに書き出すCbId値を更新する
            m_i16SerialTxCbId = (m_i16SerialTxCbId + 1) & 127;
        }
        else {
            debug_puts(DEBUG_WARNING, "SERIAL TX FULL. WAITING");
        }
    }
}

//受信スロット内のデータを処理してシリアルに書き出す
//標準モード
static void sendToSerial_SM() {
    S_RADIO_RX_SLOT *pSlot;
    
    while (1) {
        if (m_i16SerialTxCbId >= 0) {
            //次に送信したいCbId値があるとき

            //該当するスロットを探す
            while ((pSlot = findSlotByCbId((uint8_t)m_i16SerialTxCbId)) != NULL) {

                //シリアルに書き出す
                if (!serial_write(pSlot->au8Data, pSlot->u8Length)) {
                    //失敗した

                    debug_puts(DEBUG_WARNING, "SERIAL TX FULL. WAITING");

                    //他に何をやっても無駄なので一旦終了
                    break;
                }

                //スロットを空にする
                pSlot->eStatus = RADIO_RX_SLOT_EMPTY;

                //書き出したCbId値を記憶
                m_i16SentCbId = m_i16SerialTxCbId;

                //次にシリアルに書き出すCbId値を更新する
                m_i16SerialTxCbId = (m_i16SerialTxCbId + 1) & 127;
            }

            //次にシリアルに書き出したいCbId値をクリア
            m_i16SerialTxCbId = -1;
        }

        //次に送信したいスロットを探す
        if ((pSlot = getNextSend()) != NULL) {

            if (pSlot->eStatus != RADIO_RX_SLOT_WAITING_PREV_PACKET) {
                //スロットはひとつ前のデータを待っていない

                if (pSlot->bPrevData && ((pSlot->u8CbId - 1) & 127) != m_i16SentCbId) {
                    //スロットの前にデータがあって、それはシリアルに送信されていない

                    //このスロットは前のデータが受信するまで待たせる
                    pSlot->eStatus = RADIO_RX_SLOT_WAITING_PREV_PACKET;
                }
                else {
                    //このスロットはシリアルに送信可能なので、戻って処理する
                    m_i16SerialTxCbId = pSlot->u8CbId;
                    continue;
                }
            }
        }

        //時間切れのスロットを探す
        if ((pSlot = findTimeout()) != NULL) {
            //時間切れのスロットがあった

            //これを送信するために、小さい方向に連続するCbIdがあるか探す
            S_RADIO_RX_SLOT *p;
            while ((p = findSlotByCbId((pSlot->u8CbId - 1) & 127)) != NULL) {
                pSlot = p;
            }

            //連続する最も小さいCbIdを持つスロットからシリアルに送信するため、戻って処理する
            m_i16SerialTxCbId = pSlot->u8CbId;
            continue;
        }

        break;
    }
}


//空の受信スロットを取得する
static S_RADIO_RX_SLOT *getEmpty() {
    uint8_t i;
    for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus == RADIO_RX_SLOT_EMPTY) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}

//受信スロットから指定されたcbIdを持つスロットを取得する
static S_RADIO_RX_SLOT *findSlotByCbId(uint8_t cbId) {
    uint8_t i;
    for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus != RADIO_RX_SLOT_EMPTY &&
            m_arSlot[i].u8CbId == cbId) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}

//時間切れのスロットを探す
static S_RADIO_RX_SLOT *findTimeout() {
    uint8_t i;
    for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus != RADIO_RX_SLOT_EMPTY &&
            (millis() - m_arSlot[i].tReceive) >= 100) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}


//受信スロットから最も優先順位の高い(CbIdの小さい)スロットを取得する
static S_RADIO_RX_SLOT *getNextSend() {
    uint8_t arIndex[RADIO_RX_SLOT_SIZE];
    uint8_t arCbId[RADIO_RX_SLOT_SIZE];
    uint8_t len = 0;
    uint8_t i;
    for (i = 0; i < RADIO_RX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus != RADIO_RX_SLOT_EMPTY) {
            arIndex[len] = i;
            arCbId[len] = m_arSlot[i].u8CbId;
            len++;
        }
    }
    if (len == 0) return NULL;

    int16_t j = findMinValueIndexInByteGroup(arCbId, len);
    return &m_arSlot[arIndex[j]];
}

//バイト配列の中の値の分布をひとつの数値の塊とみなして、その中の最小値のインデックスを返す
//255→0は連続した値であり、絶対的な大小は無く、相対的な比較を行う
static int16_t findMinValueIndexInByteGroup(uint8_t *array, uint8_t len) {
    if (len == 0) return -1;
    if (len == 1) return 0;
    
    int16_t farestDistance = 0;
    int16_t farestIndexMax = -1;
    uint8_t i;
    for (i = 0; i < len; i++) {
        uint8_t a = *(array + i);
        int16_t nearestDistance = 1000;
        int16_t nearestIndex = -1;
        uint8_t j;
        for (j = 0; j < len; j++) {
            if (i == j) continue;
            uint8_t d = *(array + j) - a;
            if (d < nearestDistance) {
                nearestDistance = d;
                nearestIndex = j;
            }
        }
        if (nearestIndex != -1 && farestDistance < nearestDistance) {
            farestDistance = nearestDistance;
            farestIndexMax = nearestIndex;
        }
    }
    return farestIndexMax;
}
