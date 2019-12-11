#include "basicio.h"
#include "main.h"
#include "debug.h"
#include "setting.h"

//参照するグローバル変数
extern bool_t   g_bDioSettingMode;
extern bool_t   g_bTxProtectMode;
extern bool_t   g_bHwFlowControl;
extern uint16_t g_u16MyAddress;
extern uint16_t g_u16TargetAddress;
extern uint8_t  g_u8Channel;
extern bool_t   g_bDebugOutput;
extern bool_t   g_bDebugLevel;
extern bool_t   g_bDebugDevice;


//設定しない場合のデフォルト値または、シリアルから設定された値をスリープ起床後に持ち越すための変数
static bool_t   wss_bSoftwareSetting = FALSE;   //シリアルから設定された設定値を使用する
static bool_t   wss_bTxProtectMode;             //送信データが保護モードかどうか
static bool_t   wss_bHwFlowControl;             //ハードウェアフロー制御を行うかどうか
static uint16_t wss_bAddress;                   //無線通信アドレス
static uint8_t  wss_u8ChannelOffset;            //無線通信チャンネル
static uint8_t  wss_u8DebugValue;               //デバッグ出力 b0:出力 b1:レベル b2:デバイス

//設定中に使用する変数
static unsigned char m_cCmd;                    //読み取り中のコマンド
static uint8_t  m_u8Value;                      //読み取り中の値
static uint8_t  m_u8MinValue;                   //読み取り中のコマンドに設定可能な最小値
static uint8_t  m_u8MaxValue;                   //読み取り中のコマンドに設定可能な最大値
static uint8_t  m_u8RequiredDigit;              //読み取り中のコマンドの値に必要な桁数
static bool_t   m_bSet;                         //設定されたか
static E_SETTING_RESULT m_eError;               //設定にエラーがあるか、エラーの種類

//何の設定を使用しているかを記憶
static uint8_t  m_u8SettingMode;

//デフォルト／シリアル／DIOピン設定を読み込む。setup()から呼び出す
//関数は設定モードを返す 0:デフォルト 1:ソフトウェア設定 2:ハードウェア設定
uint8_t setting_load() {

    dio_pinMode(PIN_DIO_SETTING, INPUT_PULLUP);
    dio_pinMode(PIN_ADDRESS, INPUT_PULLUP);
    dio_pinMode(PIN_DEBUG_OUTPUT, INPUT_PULLUP);
    dio_pinMode(PIN_DEBUG_LEVEL, INPUT_PULLUP);
    dio_pinMode(PIN_DEBUG_DEVICE, INPUT_PULLUP);
    dio_pinMode(PIN_PROTECT_MODE, INPUT_PULLUP);
    dio_pinMode(PIN_CHANNEL_B0, INPUT_PULLUP);
    dio_pinMode(PIN_CHANNEL_B1, INPUT_PULLUP);
    dio_pinMode(PIN_HW_FLOW_CONTROL, INPUT_PULLUP);

    //DIOピンから設定するかどうか
    g_bDioSettingMode = (dio_read(PIN_DIO_SETTING) == LOW);

    if (g_bDioSettingMode) {

        //DIOピンから設定を読む
        g_bTxProtectMode = (dio_read(PIN_PROTECT_MODE) == LOW);
        g_bHwFlowControl = (dio_read(PIN_HW_FLOW_CONTROL) == LOW);
        g_u16MyAddress = BASE_ADDRESS + ((dio_read(PIN_ADDRESS) == LOW) ? 1 : 0);
        g_u16TargetAddress = BASE_ADDRESS + ((dio_read(PIN_ADDRESS) == LOW) ? 0 : 1);
        g_u8Channel = BASE_CHANNEL +
            ((dio_read(PIN_CHANNEL_B1) == LOW) ? 2 : 0) +
            ((dio_read(PIN_CHANNEL_B0) == LOW) ? 1 : 0);

        //デバッグ出力とレベルはその場でDIOピンから読むのでここでは設定しない
        g_bDebugOutput = FALSE;
        g_bDebugLevel = FALSE;
        g_bDebugDevice = (dio_read(PIN_DEBUG_DEVICE) == LOW);

        m_u8SettingMode = 2;
    }
    else if (wss_bSoftwareSetting) {

        //シリアル設定値を読む
        g_bHwFlowControl = wss_bHwFlowControl;
        g_u16MyAddress = BASE_ADDRESS + (wss_bAddress ? 1 : 0);
        g_u16TargetAddress = BASE_ADDRESS + (wss_bAddress ? 0 : 1);
        g_u8Channel = BASE_CHANNEL + wss_u8ChannelOffset;
        g_bTxProtectMode = wss_bTxProtectMode;
        g_bDebugOutput = (wss_u8DebugValue & 1) ? TRUE : FALSE;
        g_bDebugLevel = (wss_u8DebugValue & 2) ? TRUE : FALSE;
        g_bDebugDevice = (wss_u8DebugValue & 4) ? TRUE : FALSE;

        m_u8SettingMode = 1;
    }
    else {
        //デフォルト値
        g_bHwFlowControl = DEFALUT_HW_FLOW_CONTROL;
        g_u16MyAddress = BASE_ADDRESS + (DEFAULT_ADDRESS ? 1 : 0);
        g_u16TargetAddress = BASE_ADDRESS + (DEFAULT_ADDRESS ? 0 : 1);
        g_u8Channel = DEFAULT_CHANNEL;
        g_bTxProtectMode = DEFAULT_TX_PROTECT_MODE;
        g_bDebugOutput = DEFAULT_DEBUG_OUTPUT;
        g_bDebugLevel = DEFAULT_DEBUG_LEVEL;
        g_bDebugDevice = DEFAULT_DEBUG_DEVICE;

        m_u8SettingMode = 0;
    }

    //ここで setting_readFromSerial() のために変数を初期化しておく
    wss_bSoftwareSetting = FALSE;
    wss_bHwFlowControl = DEFALUT_HW_FLOW_CONTROL;
    wss_bAddress = DEFAULT_ADDRESS;
    wss_u8ChannelOffset = DEFAULT_CHANNEL - BASE_CHANNEL;
    wss_bTxProtectMode = DEFAULT_TX_PROTECT_MODE;
    wss_u8DebugValue = (DEFAULT_DEBUG_OUTPUT ? 1 : 0) + (DEFAULT_DEBUG_LEVEL ? 2 : 0) + (DEFAULT_DEBUG_DEVICE ? 4 : 0);
    m_cCmd = 0;
    m_bSet = FALSE;
    m_eError = 0;

    return m_u8SettingMode;
}

//現在の設定値を文字列で返す
const char *setting_current() {
    sb_clear();
    sb_printf("S%dH%dA%dC%dP%dD%d",
        m_u8SettingMode,                //Sは現在の設定モード。読み取り専用 0:デフォルト 1:シリアル 2:DIO
        (g_bHwFlowControl ? 1 : 0),
        g_u16MyAddress - BASE_ADDRESS,
        g_u8Channel - BASE_CHANNEL,
        (g_bTxProtectMode ? 1 : 0),
        (IS_DEBUG() ? 1 : 0) + (DEBUG_LEVEL() ? 2 : 0) + (g_bDebugDevice ? 4 : 0));
    return sb_getBuffer();
}

//シリアルから設定コマンドを読み込む
//この関数は設定コマンドと、設定モード終了コマンドを読み取って解釈する
//この関数は設定モード開始ワード"+++WSS"は解釈しない
//関数が SETTING_RESULT_COMMAND_ACCEPTED を返したときはスリープ、起床後に
// setting_load() を実行することで、シリアル設定値が有効になる
E_SETTING_RESULT setting_readFromSerial(uint16_t bytesToRead) {

    while (bytesToRead-- > 0) {

        //シリアルから１文字読み込む
        unsigned char c = (unsigned char)serial_getc();

        if (m_cCmd == 0) {
            switch (c) {
            case 'C': //チャンネル
                m_cCmd = c;
                m_u8Value = 0;
                m_u8MinValue = 0;
                m_u8MaxValue = 3;
                m_u8RequiredDigit = 1;
                break;
            case 'A': //アドレス
            case 'H': //HWフロー制御
            case 'P': //保護モード
                m_cCmd = c;
                m_u8Value = 0;
                m_u8MinValue = 0;
                m_u8MaxValue = 1;
                m_u8RequiredDigit = 1;
                break;
            case 'D': //デバッグ出力
                m_cCmd = c;
                m_u8Value = 0;
                m_u8MinValue = 0;
                m_u8MaxValue = 7;
                m_u8RequiredDigit = 1;
                break;
            case '-': //設定モード終了
                if (m_eError == 0) {
                    wss_bSoftwareSetting = TRUE;
                    return m_bSet ? SETTING_RESULT_COMMAND_ACCEPTED : SETTING_RESULT_NO_CHANGE;
                } else {
                    wss_bSoftwareSetting = FALSE;
                    return m_eError;
                }
                break;
            default: //想定しないコマンド
                m_eError |= SETTING_RESULT_UNKNOWN_COMMAND_BIT;
            }
        }
        else if (c >= '0' && c <= '9') {

            //設定値を10進数で取り込む
            m_u8Value = m_u8Value * 10 + (c - '0');

            if (--m_u8RequiredDigit == 0) {
                //全ての桁の読み込みが完了したので、値のチェック

                if (m_u8Value < m_u8MinValue || m_u8Value > m_u8MaxValue) {
                    //値が範囲外
                    m_eError |= SETTING_RESULT_VALUE_OUTOFRANGE_BIT;
                }
                else {
                    switch (m_cCmd) {
                    case 'C': //チャンネル
                        wss_u8ChannelOffset = m_u8Value;
                        m_bSet = TRUE;
                        break;
                    case 'A': //アドレス
                        wss_bAddress = (m_u8Value == 1);
                        m_bSet = TRUE;
                        break;
                    case 'H': //HWフロー制御
                        wss_bHwFlowControl = (m_u8Value == 1);
                        m_bSet = TRUE;
                        break;
                    case 'P': //保護モード
                        wss_bTxProtectMode = (m_u8Value == 1);
                        m_bSet = TRUE;
                        break;
                    case 'D': //デバッグ出力
                        wss_u8DebugValue = m_u8Value;
                        m_bSet = TRUE;
                        break;
                    }
                }
                m_cCmd = 0;
            }
        }
        else {
            //予期しない文字
            m_eError |= SETTING_RESULT_UNKNOWN_CHARACTER_BIT;
        }
    }
    return SETTING_RESULT_NOT_YET;
}

