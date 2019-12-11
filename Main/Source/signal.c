#include "basicio.h"
#include "main.h"
#include "signal.h"

#define TICK_VALUE  (1000 / TICK_COUNT)


//参照するグローバル変数
extern bool_t   g_bHwFlowControl;
extern bool_t   g_bTxProtectMode;
extern bool_t   g_bRxProtectMode;

//シグナルの状態とOFFタイミングを管理
static bool_t   m_bRadioTx;
static int16_t  m_i16RadioTxOff;

static bool_t   m_bRadioRx;
static int16_t  m_i16RadioRxOff;
static uint16_t m_u16RadioRxPower;

static bool_t   m_bTxError;
static int16_t  m_i16TxErrorOff;

static bool_t   m_bRxError;
static int16_t  m_i16RxErrorOff;



//シグナル出力の初期化
//setting_load()の後に呼び出すこと
void signal_init() {

    if (!g_bHwFlowControl) {

        //HWフロー制御を行わない場合はRTSピンを手動制御する
        dio_pinMode(PIN_REQUEST_TO_SEND, OUTPUT);

        //送信を許可
        signal_requestToSend(TRUE);
    }

    //無線送信LED 点灯 500ms
    dio_write(PIN_RADIO_TX, LOW);
    dio_pinMode(PIN_RADIO_TX, OUTPUT);
    m_bRadioTx = TRUE;
    m_i16RadioTxOff = 500;

    //無線受信LED 点灯 500ms
    timer_attachAnalogWrite(TMR_RADIO_RX, 0, DO_PIN);
    m_bRadioRx = TRUE;
    m_i16RadioRxOff = 500;
    m_u16RadioRxPower = 65535;

    //送信エラー OFF(HIGH)
    dio_write(PIN_ERROR_TX, HIGH);
    dio_pinMode(PIN_ERROR_TX, OUTPUT);
    m_bTxError = FALSE;

    //受信エラー OFF(HIGH)
    dio_write(PIN_ERROR_RX, HIGH);
    dio_pinMode(PIN_ERROR_RX, OUTPUT);
    m_bRxError = FALSE;
}

//シグナル出力のリセット（通信開始時）
void signal_reset() {

    if (!g_bHwFlowControl) {

        //送信を許可
        signal_requestToSend(TRUE);
    }

    //無線送信LED
    dio_write(PIN_RADIO_TX, HIGH);
    m_bRadioTx = FALSE;
    m_i16RadioTxOff = 0;

    //無線受信LED
    timer_updateAnalogPower(TMR_RADIO_RX, 65535);
    m_bRadioRx = FALSE;
    m_i16RadioRxOff = 0;
    m_u16RadioRxPower = 65535;

    //送信エラー
    dio_write(PIN_ERROR_TX, HIGH);
    m_bTxError = FALSE;

    //受信エラー
    dio_write(PIN_ERROR_RX, HIGH);
    m_bRxError = FALSE;
}

//EVENT_TICK_TIMER 毎に呼び出すこと
void signal_update() {

    if (m_bRadioTx) {
        m_i16RadioTxOff -= TICK_VALUE;
        if (m_i16RadioTxOff <= 0) {
            m_bRadioTx = FALSE;
            //無線送信LEDの消灯
            dio_write(PIN_RADIO_TX, HIGH);
        }
    }

    if (m_bRadioRx) {
        m_i16RadioRxOff -= TICK_VALUE;
        if (m_i16RadioRxOff <= 0) {
            m_bRadioRx = FALSE;
            //無線受信LEDの点灯
            timer_updateAnalogPower(TMR_RADIO_RX, m_u16RadioRxPower);
        }
    }

    if (m_bTxError && m_i16TxErrorOff > 0) {
        m_i16TxErrorOff -= TICK_VALUE;
        if (m_i16TxErrorOff <= 0) {
            m_bTxError = FALSE;
            //送信エラーの復帰
            dio_write(PIN_ERROR_TX, HIGH);
        }
    }

    if (m_bRxError && m_i16RxErrorOff > 0) {
        m_i16RxErrorOff -= TICK_VALUE;
        if (m_i16RxErrorOff <= 0) {
            m_bRxError = FALSE;
            //受信エラーの復帰
            dio_write(PIN_ERROR_RX, HIGH);
        }
    }
}


//HWフロー制御を行わない場合で、TRUEを渡すと送信許可(LOW)になり、
//FALSEを渡すと送信禁止(HIGH)になる
//出力が変化したときにTRUEを返す
bool_t signal_requestToSend(bool_t b) {
    if (g_bHwFlowControl) return FALSE;

    static bool_t cur;
    bool_t r = (b != cur);
    cur = b;
    dio_write(PIN_REQUEST_TO_SEND, b ? LOW : HIGH);
    return r;
}

//無線送信したときに呼び出す
void signal_radioTx() {
    if (m_bRadioTx) return; //点灯しっぱなしを避ける

    dio_write(PIN_RADIO_TX, LOW);
    m_bRadioTx = TRUE;
    m_i16RadioTxOff = 50; //[ms]
}

//無線受信したときに呼び出す
void signal_radioRx() {
    if (m_bRadioRx) return; //消灯しっぱなしを避ける

    timer_updateAnalogPower(TMR_RADIO_RX, 65535);
    m_bRadioRx = TRUE;
    m_i16RadioRxOff = 50; //[ms]
}

//無線信号品質を表すLQI値を渡す
void signal_setLqi(uint8_t lqi) {
    uint16_t pw;
    if (lqi >= 150) {
        pw = 0;              //アンテナ近傍 明るさ100%
    } else if (lqi >= 100) {
        pw = 65535 * 0.25;   //良好 明るさ75%
    } else if (lqi >= 50) {
        pw = 32768;          //やや悪い 明るさ50%
    } else if (lqi > 0) {
        pw = 65535 * 0.75;   //悪い 明るさ25%
    } else {
        pw = 65535;          //消灯
    }
    if (pw != m_u16RadioRxPower) {
        //値が変化したときだけ処理

        m_u16RadioRxPower = pw;

        if (!m_bRadioRx) {
            //点灯中であるので明るさを反映
            timer_updateAnalogPower(TMR_RADIO_RX, m_u16RadioRxPower);
        }
    }
}

//シリアル受信～無線送信でエラーやデータ欠落があった場合に呼び出す
void signal_txError() {
    if (!g_bTxProtectMode) {
        if (m_bTxError) return; //点灯しっぱなしを避ける
        dio_write(PIN_ERROR_TX, LOW);
        m_bTxError = TRUE;
        m_i16TxErrorOff = 50; //[ms]
    }
    else {
        dio_write(PIN_ERROR_TX, LOW);
        m_bTxError = TRUE;
        m_i16TxErrorOff = 0;             //連続ON
    }
}

//無線受信～シリアル送信でエラーやデータ欠落があった場合に呼び出す
void signal_rxError() {
    if (!g_bRxProtectMode) {
        if (m_bRxError) return; //点灯しっぱなしを避ける
        dio_write(PIN_ERROR_RX, LOW);
        m_bRxError = TRUE;
        m_i16RxErrorOff = 50; //[ms]
    }
    else {
        dio_write(PIN_ERROR_RX, LOW);
        m_bRxError = TRUE;
        m_i16RxErrorOff = 0;             //連続ON
    }
}
