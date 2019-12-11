/*
 * ワイヤレスシリアル
 * v0.1 2019/11/20 by totsucom
 */
#include "basicio.h"
#include "main.h"
#include "signal.h"
#include "setting.h"
#include "radioTx.h"
#include "radioRx.h"
#include "debug.h"


#define DEFAULT_BAUDRATE        
#define DEFAULT_APPID           0x55553201

#define PIN_SLEEP               0

/* setting_load()で初期化される */
bool_t   g_bDioSettingMode;
bool_t   g_bTxProtectMode;
bool_t   g_bHwFlowControl;
uint16_t g_u16MyAddress;
uint16_t g_u16TargetAddress;
uint8_t  g_u8Channel;
bool_t   g_bDebugOutput;    //デバッグ出力判定は変数を直接参照せずに IS_DEBUG() マクロを使うこと
bool_t   g_bDebugLevel;     //デバッグレベルの取得は変数を直接参照せずに DEBUG_LEVEL() マクロを使うこと
bool_t   g_bDebugDevice;    //FALSEでシリアル０、TRUEでシリアル１に出力する

/* 相手から通信開始パケットを受け取ったとき初期化される */
bool_t   g_bRxProtectMode;

/* ビーコン制御 */
uint32_t g_tLastRadioSend;              //無線送信した時間
static uint32_t m_tLastRadioReceive;    //無線受信した時間

/* 無線通信ステータス（最初のハンドシェイクが行われるまで） */
typedef enum {
    CON_STATUS_NONE,
    CON_STATUS_REQUESTING,
    CON_STATUS_WAITING_REPLY,
    CON_STATUS_REPLYING,
    CON_STATUS_CONNECTED
} CONNECTIONSTATUS;
static CONNECTIONSTATUS m_eConStatus;
static uint8_t m_u8ConCbId;

/* 重複受信を回避するためにCbId値を記憶するバッファ */
typedef struct {
    int16_t  i16CbId;
    uint32_t tReceive;
    bool_t   bNoTimeout;
} CBIDHISTORY;
#define CBID_HISTORY_SIZE  5
static CBIDHISTORY arCbIdHist[CBID_HISTORY_SIZE];

/* モジュール変数 */
bool_t m_bSettingMode;
bool_t m_bWaitingForReboot;
uint8_t m_u8ConReplyRetry;              //通信開始リプライのリトライ残数
int16_t m_i16ConReqTiming;              //通信開始要求の送信タイミングカウンタ。単位ms

/* ローカル関数のプロトタイプ */
void rebootSleep();
void sleepFunc();
void clearCbIdHistory();
void txFunc(uint8_t u8CbId, bool_t bSuccess);
bool_t judgeFunc(uint32_t u32SrcAddr,uint8_t u8CbId);
void rxFunc(uint32_t u32SrcAddr,bool_t bBroadcast,uint8_t u8CbId,
        uint8_t u8DataType,uint8_t *pu8Data,uint8_t u8Length,uint8_t u8Lqi);


void setup(bool_t warmWake, uint32_t bitmapWakeStatus) {

    /* ローカル変数の初期化 */

    g_tLastRadioSend = 0;
    m_tLastRadioReceive = 0;
    m_eConStatus = CON_STATUS_NONE;
    m_bSettingMode = FALSE;
    m_bWaitingForReboot = FALSE;
    m_i16ConReqTiming = 0;


    /* モジュール初期化 */

    uint8_t settingMode = setting_load();
    signal_init();
    radioTx_init();
    debug_init();
    clearCbIdHistory();


    /* シリアル初期化 */

    serial_initEx(
        SERIAL_BAUD_38400,
        SERIAL_PARITY_NONE,
        SERIAL_LENGTH_8BITS,
        SERIAL_STOP_1BIT,
        FALSE,  //bUseSecondPin
        g_bHwFlowControl ? SERIAL_HWFC_TIMER4 : SERIAL_HWFC_NONE);

    if (g_bDebugDevice) {
        serial1_initEx(
            SERIAL_BAUD_38400,
            SERIAL_PARITY_NONE,
            SERIAL_LENGTH_8BITS,
            SERIAL_STOP_1BIT,
            TRUE,   //bUseTxOnly
            FALSE); //bUseSecondPin
    }


    /* 設定情報の出力 */
    
    debug_begin();

    sb_clear();
    sb_puts("BOOT-MODE:");
    sb_puts(warmWake ? "WAKE-UP" : "POWER-ON");
    sb_puts(" SETTING:");
    switch (settingMode) {
    case 0: sb_puts("DEFAULT"); break;
    case 1: sb_puts("SERIAL"); break;
    case 2: sb_puts("DIO"); break;
    }
    debug_puts(DEBUG_INFO, sb_getBuffer());

    sb_clear();
    sb_puts("MY-ADDR:0x");
    sb_printf("%x", g_u16MyAddress);
    sb_puts(" TARGET-ADDR:0x");
    sb_printf("%x", g_u16TargetAddress);
    sb_puts(" CHANNEL:");
    sb_printf("%d", g_u8Channel);
    sb_puts(" FLOW-CONTROL:");
    sb_puts(g_bHwFlowControl ? "YES" : "NO");
    sb_puts(" TX-DATA-MODE:");
    sb_puts(g_bTxProtectMode ? "PROTECT" : "STANDARD");
    sb_puts(" DEBUG-LEVEL:");
    sb_puts(DEBUG_LEVEL() ? "ALL" : "ERROR-ONLY");
    debug_puts(DEBUG_INFO, sb_getBuffer());

    debug_end();


    //スリープ用DIO割り込みを設定
    dio_pinMode(PIN_SLEEP, INPUT_PULLUP);
    dio_attachCallback(PIN_SLEEP, FALLING, sleepFunc);


    /* 無線初期化 */

    radio_setupInit(RADIO_MODE_TXRX, DEFAULT_APPID, g_u8Channel, 3);
    radio_setupShortAddress(g_u16MyAddress);
    radio_attachCallback(txFunc, rxFunc);
    radio_setRetry(1, 0);   //retry count, duration
    radio_replaceRxDupJudgeCallback(judgeFunc);
}

void loop(EVENTS event) {

    if (event == EVENT_START_UP) {

        //通信開始パケットを送信
        m_u8ConCbId = radioTx_sendConnectionStart(FALSE);

        //通信開始要求中
        m_eConStatus = CON_STATUS_REQUESTING;
        debug_puts(DEBUG_INFO, "CONNECTION REQUESTING...");

    } else if (event == EVENT_TICK_SECOND) {
        // 1秒毎に呼ばれる


        //debug_printf(DEBUG_INFO, "STATUS:%d TIMIG:%d", m_eConStatus, m_i16ConReqTiming);



    } else if (event == EVENT_TICK_TIMER) {
        // １ミリ秒毎に呼ばれる

        /* シリアル読み込み処理 */

        //シリアル読み込みバッファのデータバイト数を得る
        uint16_t bytesToRead = serial_getRxCount();

        if (bytesToRead > 0) {
            if (!m_bSettingMode) {

                //シリアルデータの読み取り処理
                m_bSettingMode = radioTx_readFromSerial(bytesToRead);

                if (m_bSettingMode) {
                    //設定モードに入ったら現在値を返す
                    serial_puts(setting_current());
                    serial_puts("\r\n");
                }

            } else {

                //設定コマンドの読み取り処理
                E_SETTING_RESULT r = setting_readFromSerial(bytesToRead);

                if (r != SETTING_RESULT_NOT_YET) {
                    //設定モードが終了した

                    if (SETTING_RESULT_HAS_ERROR(r)) {
                        //エラーあり

                        /* エラーはデバッグモードに関係なく出力する */

                        serial_puts("\r\n");

                        //デバッグがシリアル１の場合はそちらにも出力
                        if (g_bDebugDevice) debug_begin();

                        if (r & SETTING_RESULT_UNKNOWN_COMMAND_BIT) {
                            //不明なコマンド

                            serial_puts("[E] UNKNOWN COMMAND\r\n");
                            if (g_bDebugDevice) debug_puts(DEBUG_ERROR, "UNKNOWN COMMAND");
                        }
                        if (r & SETTING_RESULT_VALUE_OUTOFRANGE_BIT) {
                            //値が範囲外

                            serial_puts("[E] VALUE OUT OF RANGE\r\n");
                            if (g_bDebugDevice) debug_puts(DEBUG_ERROR, "VALUE OUT OF RANGE");
                        }
                        if (r & SETTING_RESULT_UNKNOWN_CHARACTER_BIT) {
                            //不明な文字

                            serial_puts("[E] UNKNOWN CHARACTER\r\n");
                            if (g_bDebugDevice) debug_puts(DEBUG_ERROR, "UNKNOWN CHARACTER");
                        }
                        if (g_bDebugDevice) debug_end();

                        //再起動フラグを立てる
                        m_bWaitingForReboot = TRUE;
                    }
                    else if (r == SETTING_RESULT_COMMAND_ACCEPTED) {
                        //設定コマンドを受け付けた

                        //再起動フラグを立てる
                        m_bWaitingForReboot = TRUE;
                        debug_puts(DEBUG_INFO, "SETTING COMMAND ACCEPTED");
                    }
                    else {
                        //設定されなかった
                        debug_puts(DEBUG_INFO, "SETTING MODE EXITED");
                    }

                    //設定モードから抜ける
                    m_bSettingMode = FALSE;
                }
            }
        } else {

            //シリアルから読み込んだデータが残っていれば送信対象にする
            radioTx_sendBalance();
        }

        if (!m_bWaitingForReboot) { //再起動待ちの場合はシリアルに書き出さない

            //無線受信データをシリアルに書き出す
            radioRx_sendToSerial();
        }

        if (m_eConStatus == CON_STATUS_CONNECTED) {

            //無線送信処理
            radioTx_send();

            //無線送信状況のチェック
            if ((millis() - g_tLastRadioSend) > 2000) {
                //前回の何かしらの送信から２秒が経過した

                //ビーコン送信
                radioTx_sendBeacon();
            }

            //無線受信状況のチェック
            if ((millis() - m_tLastRadioReceive) > 3000) {
                //前回の受信から３秒が経過した

                //受信状態LEDを消灯
                signal_setLqi(0);
            }
        }

        //出力信号(LEDなど)の処理
        signal_update();

        if (serial_dataLost()) {
            //シリアル受信バッファでデータ欠落（バッファフル）

            signal_txError();

            debug_puts(DEBUG_ERROR, "SERIAL RX FULL. DATA LOST");
        }

        if (m_eConStatus == CON_STATUS_NONE && m_i16ConReqTiming > 0) {

            m_i16ConReqTiming -= getTickPeriod();
            if (m_i16ConReqTiming <= 0) {
                //通信開始要求のタイミングがきた

                //通信開始パケットを送信
                m_u8ConCbId = radioTx_sendConnectionStart(FALSE);

                //通信開始要求中
                m_eConStatus = CON_STATUS_WAITING_REPLY;
            }
        }


        //再起動はシリアルの出力完了を待つ
        if (m_bWaitingForReboot && serial_getTxCount() == 0 &&
            (!g_bDebugDevice || (g_bDebugDevice && serial1_getTxCount() == 0))) {

            //再起動処理
            rebootSleep();
        }
    }
}

//リブート目的のスリープ
void rebootSleep() {

    //シリアル０停止
    serial_disable();

    //シリアル１停止
    if (g_bDebugDevice) {
        serial1_disable();
    }

    //コールバックを停止
    dio_detach(PIN_SLEEP);

    //アナログ出力を停止
    timer_detach(TMR_RADIO_RX);

    //スリープ
    sleepTimer(10, TRUE);           //RAMは保持
}

//PIN_SLEEPにLOWが入力されたときに呼び出される
void sleepFunc() {

    //シリアル０停止
    serial_disable();

    //シリアル１停止
    if (g_bDebugDevice) {
        serial1_disable();
    }

    //コールバックを停止
    dio_detach(PIN_SLEEP);

    //アナログ出力を停止
    timer_detach(TMR_RADIO_RX);

    //スリープ
    dio_setWake(PIN_SLEEP, RISING); //PIN_SLEEPがHIGHに戻ったら起床する
    sleep(FALSE, TRUE);             //RAMは保持
}

//無線送信が完了したときに呼び出される
void txFunc(uint8_t u8CbId, bool_t bSuccess) {

    if (u8CbId == 255) {
        //ビーコンの場合は何もしない

    }
    else if (m_eConStatus == CON_STATUS_REQUESTING && m_u8ConCbId == u8CbId) {
        if (bSuccess) {
            //通信開始要求が相手に届いたので待つ
            m_eConStatus = CON_STATUS_WAITING_REPLY;
        }
        else {
            //通信開始要求が届かなかったので再送信

            //通信開始パケットを再送信
            m_i16ConReqTiming = 250;
        }
    }
    else if (m_eConStatus == CON_STATUS_REPLYING && m_u8ConCbId == u8CbId) {
        if (bSuccess) {
            //通信開始リプライが相手に届いたので完了
            m_eConStatus = CON_STATUS_CONNECTED;

            //出力信号をリセット
            signal_reset();

            if (g_bRxProtectMode) {
                debug_printf(DEBUG_INFO, "CONNECTED. NextSendID:%X NextRecvID:%X", radioTx_getNextCbId(), radioRx_getNextCbId());
            } else {
                debug_printf(DEBUG_INFO, "CONNECTED. NextSendID:%X", radioTx_getNextCbId());
            }
        }
        else {
            //通信開始リプライが届かなかったので再送信

            if (m_u8ConReplyRetry > 0) { //相手からの次の通信開始要求と重ならないようにリトライ回数を設けている

                //通信開始リプライパケットを再送信
                m_u8ConCbId = radioTx_sendConnectionStart(TRUE);

                m_u8ConReplyRetry--;
            }
            else {
                //通信開始リプライに失敗した

                //相手からの次の通信開始要求まで未接続状態にする
                m_eConStatus = CON_STATUS_NONE;
            }
        }
    }
    else if (m_eConStatus == CON_STATUS_CONNECTED) {

        //無線送信結果を渡す
        radioTx_sendResult(u8CbId, bSuccess);
    }
}


//受信履歴バッファを初期化する
void clearCbIdHistory() {
    uint8_t i;
    for (i = 0; i < CBID_HISTORY_SIZE; i++) {
        arCbIdHist[i].i16CbId = -1;
    }
}

//重複受信回避処理のために呼ばれる
bool_t judgeFunc(uint32_t u32SrcAddr,uint8_t u8CbId) {

    if (u32SrcAddr != g_u16TargetAddress) {
        //相手以外からの受信は受け付けない
        return FALSE;
    }

    int8_t emptyIndex = -1;
    int8_t oldestIndex = -1;
    uint32_t passTime = 0;
    int8_t i;
    for (i = 0; i < CBID_HISTORY_SIZE; i++) {
        if (arCbIdHist[i].i16CbId != -1) {
            uint32_t t = millis() - arCbIdHist[i].tReceive;
            if (t >= 200 && !arCbIdHist[i].bNoTimeout) {
                //受信データが保護モード以外では、200msで履歴が消える
                arCbIdHist[i].i16CbId = -1;
                emptyIndex = i;
            } else {
                if (arCbIdHist[i].i16CbId == u8CbId) {
                    //ヒストリに同じCbIdがあった
                    arCbIdHist[i].tReceive = millis();
                    return FALSE;
                }
                if (t > passTime) {
                    //古いデータを記憶
                    passTime = t;
                    oldestIndex = i;
                }
            }
        } else {
            emptyIndex = i;
        }
    }

    //保護モードでの受信データは有効期限は無い
    bool_t bNoTimeout = (m_eConStatus == CON_STATUS_CONNECTED && u8CbId != 255 && g_bRxProtectMode);

    if (emptyIndex >= 0) {
        //空の履歴があるのでそこに保存
        arCbIdHist[emptyIndex].i16CbId = u8CbId;
        arCbIdHist[emptyIndex].tReceive = millis();
        arCbIdHist[emptyIndex].bNoTimeout = bNoTimeout;
    } else {
        //最も古い履歴に上書き
        arCbIdHist[oldestIndex].i16CbId = u8CbId;
        arCbIdHist[oldestIndex].tReceive = millis();
        arCbIdHist[oldestIndex].bNoTimeout = bNoTimeout;
    }
    return TRUE;
}

//無線受信したときに呼び出される
void rxFunc(uint32_t u32SrcAddr,bool_t bBroadcast,uint8_t u8CbId,
        uint8_t u8DataType,uint8_t *pu8Data,uint8_t u8Length,uint8_t u8Lqi) {

    //受信時刻を記憶
    m_tLastRadioReceive = millis();

    //LQI値をLEDの明るさに反映
    signal_setLqi(u8Lqi);


    if (u8CbId == 255) {
        //ビーコン

        if (m_eConStatus == CON_STATUS_CONNECTED) {
            //LQI値をLEDの明るさに反映
            signal_setLqi(u8Lqi);
        }

        return;
    }

    if ((u8DataType & PACKET_DATATYPE_MASK) == PACKET_DATATYPE_CONNECTION_START) {
        //相手からの通信開始要求パケットを受信した

        if (m_eConStatus == CON_STATUS_WAITING_REPLY) {
            //自分は相手からのリプライパケットを待っている

            //処理が相手と重複したため、通信開始要求をずらして再送信する
            m_i16ConReqTiming = 50 + (random() & 255);

            m_eConStatus = CON_STATUS_NONE;
            debug_puts(DEBUG_INFO, "CONNECTION CONFLICTING");
        }
        else {
            //受信データは保護モードか
            g_bRxProtectMode = u8DataType & PACKET_DATATYPE_PROTECTMODE_BIT;

            //受信スロットを初期化
            radioRx_init((u8CbId + 1) & 127);

            //重複受信回避処理の履歴をクリア
            clearCbIdHistory();

            //通信開始リプライパケットを送信
            m_u8ConCbId = radioTx_sendConnectionStart(TRUE);
            m_u8ConReplyRetry = 1;  //１回リトライでだめっだったら相手からの通信要求を再度待つ

            //リプライ中
            m_eConStatus = CON_STATUS_REPLYING;
            debug_puts(DEBUG_INFO, "CONNECTION REPLYING...");
        }
    }
    else if (m_eConStatus == CON_STATUS_WAITING_REPLY &&
        (u8DataType & PACKET_DATATYPE_MASK) == PACKET_DATATYPE_CONNECTION_REPLY) {
        //相手からの通信開始リプライパケットを受信した

        //受信データは保護モードか
        g_bRxProtectMode = u8DataType & PACKET_DATATYPE_PROTECTMODE_BIT;

        //受信スロットを初期化
        radioRx_init((u8CbId + 1) & 127);

        //重複受信回避処理の履歴をクリア
        clearCbIdHistory();

        //通信開始リプライが届いたので完了
        m_eConStatus = CON_STATUS_CONNECTED;

        //出力信号をリセット
        signal_reset();

        if (g_bRxProtectMode) {
            debug_printf(DEBUG_INFO, "CONNECTED. NextSendID:%X NextRecvID:%X", radioTx_getNextCbId(), radioRx_getNextCbId());
        } else {
            debug_printf(DEBUG_INFO, "CONNECTED. NextSendID:%X", radioTx_getNextCbId());
        }
    }
    else if (m_eConStatus == CON_STATUS_CONNECTED &&
        (u8DataType & PACKET_DATATYPE_MASK) == PACKET_DATATYPE_DATA) {
        //ユーザーデータ

        //受信スロットにデータを渡す
        radioRx_add(u8CbId, u8DataType, pu8Data, u8Length);        
    }
}
