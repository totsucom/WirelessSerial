#include "basicio.h"
#include "main.h"
#include "signal.h"
#include "radioTx.h"
#include "debug.h"

//参照するグローバル変数
extern bool_t   g_bDioSettingMode;
extern bool_t   g_bTxProtectMode;
extern uint16_t g_u16TargetAddress;
extern uint32_t g_tLastRadioSend;


typedef enum {
    RADIO_TX_SLOT_EMPTY,
    RADIO_TX_SLOT_ADDING,
    RADIO_TX_SLOT_WAIT_FOR_SEND,
    RADIO_TX_SLOT_WAIT_FOR_RESULT,
    RADIO_TX_SLOT_WAIT_FOR_RETRY
} E_RADIO_TX_SLOT_STATUS;

typedef struct {
    E_RADIO_TX_SLOT_STATUS  eStatus;
    uint8_t                 u8CbId;
    uint8_t                 u8Order;
    uint8_t                 au8Data[PACKET_LENGTH_MAX];
    uint8_t                 u8Length;
    uint8_t                 u8Retry;
} S_RADIO_TX_SLOT;

//送信スロットの実体
#define RADIO_TX_SLOT_SIZE  5
static S_RADIO_TX_SLOT m_arSlot[RADIO_TX_SLOT_SIZE];

//追加中のスロット
static S_RADIO_TX_SLOT *m_pAdding;

//次にスロットのu8CbIdに設定するCbId値
static uint8_t m_u8NextProvideCbId;     //0-127 通信開始のハンドシェイクでもインクリメントする

//次にスロットのu8CbIdに設定するOrder値(送信順番)
static uint8_t m_u8NextProvideOrder;    //0-255 データがスロットで送信待ちになったときに割り当て、インクリメントする

//次に無線送信するスロットのu8Order値を記憶
static uint8_t m_u8NextSendOrder;       //0-255 データ送信毎にインクリメントする

//設定モードへ入るワードを受け付けるか
static bool_t m_bAcceptEnterSettingMode;

//設定モードへ入るワード
#define WORD_ENTER_SETTING_MODE  "+++WSS"

//直前に送信されたデータがある
static bool_t m_bTxPrevData;


/* ローカル関数のプロトタイプ */

//空のスロットを返す
static S_RADIO_TX_SLOT *getEmptySlot();

//指定されたu8CbId値を持つスロットを返す
static S_RADIO_TX_SLOT *findSlotByCbId(uint8_t u8CbId);

//指定されたu8Order値を持つスロットを返す
static S_RADIO_TX_SLOT *findSlotByOrder(uint8_t u8Order);

//次のデータがあるか（標準モード用関数なのでリトライデータはないものとする）
static bool_t hasNextData();

//リトライ送信中がスロット内にある
static bool_t hasRetrying();

//リトライ送信待ちから最も優先順位の高い(CbIdの小さい)スロットを取得する
static S_RADIO_TX_SLOT *getNextRetry();

//バイト配列の中の値の分布をひとつの数値の塊とみなして、その中の最小値のインデックスを返す
//255→0は連続した値であり、絶対的な大小は無く、相対的な比較を行う
static int16_t findMinValueIndexInByteGroup(uint8_t *array, uint8_t len);



//モジュール変数の初期化
//setting_load() 後に呼び出すこと
void radioTx_init() {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        m_arSlot[i].eStatus = RADIO_TX_SLOT_EMPTY;
    }
    m_pAdding = NULL;
    m_u8NextProvideCbId = 0;
    m_u8NextProvideOrder = 0;
    m_u8NextSendOrder = 0;
    m_bAcceptEnterSettingMode = g_bDioSettingMode ? FALSE : TRUE;
    m_bTxPrevData = FALSE;
}

//次に送信するCbId値を返す
uint8_t radioTx_getNextCbId() {
    return m_u8NextProvideCbId;
}

//シリアルからデータを読んで送信スロットに格納する
//設定モードへ入るワードを受けた場合はTRUEを返す
bool_t radioTx_readFromSerial(uint16_t bytesToRead) {

    while (bytesToRead > 0) {

        if (m_pAdding == NULL) {

            //空きスロットを取得する
            m_pAdding = getEmptySlot();

            if (m_pAdding == NULL) {
                //空きスロットが無い

                //送信禁止を要求
                if (signal_requestToSend(FALSE)) {

                    debug_puts(DEBUG_WARNING, "REQUEST TO SEND: INACTIVE");
                }

                return FALSE;
            }

            //スロットを初期化
            m_pAdding->eStatus = RADIO_TX_SLOT_ADDING;
            m_pAdding->u8Length = 0;

            //送信許可
            if (signal_requestToSend(TRUE)) {

                debug_puts(DEBUG_WARNING, "REQUEST TO SEND: ACTIVE");
            }
        }

        //シリアルから１文字読み取ってスロットに保存
        m_pAdding->au8Data[m_pAdding->u8Length++] = (uint8_t)serial_getc();
        bytesToRead--;

        if (m_bAcceptEnterSettingMode) {
            //設定モードへ入るワードを受付中

            //これまで入力された文字と設定モードへ入るワードを部分比較
            m_bAcceptEnterSettingMode
                = (memcmp((const void *)(&m_pAdding->au8Data[0]),
                        (const void *)WORD_ENTER_SETTING_MODE,
                        (size_t)(m_pAdding->u8Length)) == 0);

            if (m_bAcceptEnterSettingMode &&
                m_pAdding->u8Length == (sizeof(WORD_ENTER_SETTING_MODE) - 1)) {
                //完全一致したので設定モードに入る

                //書き込み中のスロットを破棄
                m_pAdding->eStatus = RADIO_TX_SLOT_EMPTY;
                m_pAdding = NULL;

                //設定モードに入ったことを返す
                return TRUE;
            }
        }

        if (m_pAdding->u8Length == PACKET_LENGTH_MAX) {
            //スロットのバッファがいっぱいになった

            //書き込み中のスロットを送信待ちに変更
            m_pAdding->eStatus = RADIO_TX_SLOT_WAIT_FOR_SEND;
            m_pAdding->u8Retry = 0;
            m_pAdding->u8CbId = m_u8NextProvideCbId;
            m_pAdding->u8Order = m_u8NextProvideOrder++;

            //次に設定するCbId値を更新
            m_u8NextProvideCbId = (m_u8NextProvideCbId + 1) & 127;

            m_pAdding = NULL;
        }
    }
    return FALSE;
}

//書き込み中のスロットがあれば送信対象にする
void radioTx_sendBalance() {
    if (m_pAdding == NULL) return;

    if (m_bAcceptEnterSettingMode) {
        //入力中のデータが設定モードに入るワードの可能性が場合
        return;
    }

    if (m_pAdding->u8Length > 0) {
        //書き込み中のスロットを送信待ちに変更
        m_pAdding->eStatus = RADIO_TX_SLOT_WAIT_FOR_SEND;
        m_pAdding->u8Retry = 0;
        m_pAdding->u8CbId = m_u8NextProvideCbId;
        m_pAdding->u8Order = m_u8NextProvideOrder++;

        //次に設定するCbId値を更新
        m_u8NextProvideCbId = (m_u8NextProvideCbId + 1) & 127;
    }
    m_pAdding = NULL;

}

//ビーコンパケットを送信する
void radioTx_sendBeacon() {
    uint8_t dummy;

    //CbIdを設定
    radio_setCbId(255);

    //無線送信
    radio_write(g_u16TargetAddress, 0, &dummy, 1);

    g_tLastRadioSend = millis();
}

//通信開始パケットを送信する
uint8_t radioTx_sendConnectionStart(bool_t bReply) {
    uint8_t dummy;

    //CbIdを設定
    uint8_t cbId = m_u8NextProvideCbId; 
    radio_setCbId(cbId);

    //無線送信
    radio_write(g_u16TargetAddress,
        (bReply ? PACKET_DATATYPE_CONNECTION_REPLY : PACKET_DATATYPE_CONNECTION_START)
            + (g_bTxProtectMode ? PACKET_DATATYPE_PROTECTMODE_BIT : 0),
        &dummy,
        1);

    g_tLastRadioSend = millis();

    //次に設定するCbId値を更新
    m_u8NextProvideCbId = (m_u8NextProvideCbId + 1) & 127;

    return cbId;
}

//送信スロットの内の優先順位に応じて無線送信を行う
//通信開始されていない間は呼び出すべきでない
void radioTx_send() {
    S_RADIO_TX_SLOT *pSlot;

    //同時３送信まで対応
    if (radio_txCount() >= 3) return;

    if (g_bTxProtectMode) {

        //リトライ送信中は新たに追加しない
        if (hasRetrying()) return;

        //最も優先順位の高いリトライスロットを取得
        S_RADIO_TX_SLOT *pSlot = getNextRetry();

        if (pSlot != NULL) {

            //CbIdを設定
            radio_setCbId(pSlot->u8CbId);

            //無線送信
            radio_write(g_u16TargetAddress,
                PACKET_DATATYPE_DATA,
                pSlot->au8Data,
                pSlot->u8Length);

            g_tLastRadioSend = millis();

            debug_printf(DEBUG_INFO, "RETRY ID:%02X %dB", pSlot->u8CbId, pSlot->u8Length);

            //スロットを送信結果待ちに変更
            pSlot->eStatus = RADIO_TX_SLOT_WAIT_FOR_RESULT;

            //送信シグナル
            signal_radioTx();
            return;
        }
    }

    //次に送信したいCbIdを探す
    pSlot = findSlotByOrder(m_u8NextSendOrder);

    if (pSlot != NULL && pSlot->eStatus == RADIO_TX_SLOT_WAIT_FOR_SEND) {

        //CbIdを設定
        radio_setCbId(pSlot->u8CbId);

        //無線送信
        radio_write(g_u16TargetAddress,
            PACKET_DATATYPE_DATA + (m_bTxPrevData ? PACKET_DATATYPE_PREVDATA_BIT : 0),
            pSlot->au8Data,
            pSlot->u8Length);

        g_tLastRadioSend = millis();

        debug_printf(DEBUG_INFO, "SEND  ID:%02X %dB", pSlot->u8CbId, pSlot->u8Length);

        //スロットを送信結果待ちに変更
        pSlot->eStatus = RADIO_TX_SLOT_WAIT_FOR_RESULT;

        //送信シグナル
        signal_radioTx();

        //次に送信したいOrder値を更新
        m_u8NextSendOrder++;

        if (!g_bTxProtectMode) {

            //標準モードで次の無線送信のために、フラグを準備
            m_bTxPrevData = hasNextData();
        }
    }
}

//ユーザーデータの無線送信結果を渡す
void radioTx_sendResult(uint8_t u8CbId, bool_t bSuccess) {
    
    S_RADIO_TX_SLOT *pSlot = findSlotByCbId(u8CbId);
    if (!pSlot || pSlot->eStatus != RADIO_TX_SLOT_WAIT_FOR_RESULT) return; //ありえない
    
    if (bSuccess) {

        //成功したのでスロットから削除する
        pSlot->eStatus = RADIO_TX_SLOT_EMPTY;
    }
    else if (!g_bTxProtectMode) {

        //標準モードで失敗したらスロットから削除する
        pSlot->eStatus = RADIO_TX_SLOT_EMPTY;

        //データが無くなったので送信エラーを発報
        signal_txError();

        debug_puts(DEBUG_ERROR, "RADIO TX NOT REACH. DATA LOST");
    }
    else {

        //保護モードの場合は再リトライに設定する
        pSlot->eStatus = RADIO_TX_SLOT_WAIT_FOR_SEND;
        pSlot->u8Retry++;
    }
}



//空のスロットを返す
static S_RADIO_TX_SLOT *getEmptySlot() {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus == RADIO_TX_SLOT_EMPTY) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}

//指定されたu8CbId値を持つスロットを返す
static S_RADIO_TX_SLOT *findSlotByCbId(uint8_t u8CbId) {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus != RADIO_TX_SLOT_EMPTY &&
            m_arSlot[i].u8CbId == u8CbId) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}

//指定されたu8Order値を持つスロットを返す
static S_RADIO_TX_SLOT *findSlotByOrder(uint8_t u8Order) {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus != RADIO_TX_SLOT_EMPTY &&
            m_arSlot[i].u8Order == u8Order) {
            return &m_arSlot[i];
        }
    }
    return NULL;
}

//次のデータがあるか（標準モード用関数なのでリトライデータはないものとする）
static bool_t hasNextData() {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus == RADIO_TX_SLOT_WAIT_FOR_SEND ||
            m_arSlot[i].eStatus == RADIO_TX_SLOT_ADDING) {
            //送信待ちまたは追加中のデータがある
            return TRUE;
        }
    }
    return (serial_getRxCount() > 0); //シリアルにデータがある
}


//リトライ送信中がスロット内にある
static bool_t hasRetrying() {
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus == RADIO_TX_SLOT_WAIT_FOR_RESULT &&
            m_arSlot[i].u8Retry > 0) {
            return TRUE;
        }
    }
    return FALSE;
}

//リトライ送信待ちから最も優先順位の高い(CbIdの小さい)スロットを取得する
static S_RADIO_TX_SLOT *getNextRetry() {
    uint8_t arIndex[RADIO_TX_SLOT_SIZE];
    uint8_t arCbId[RADIO_TX_SLOT_SIZE];
    uint8_t len = 0;
    uint8_t i;
    for (i = 0; i < RADIO_TX_SLOT_SIZE; i++) {
        if (m_arSlot[i].eStatus == RADIO_TX_SLOT_WAIT_FOR_SEND &&
            m_arSlot[i].u8Retry > 0) {
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
