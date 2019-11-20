/*
 * ワイヤレスシリアル
 * v0.1 2019/11/20 by totsucom
 */
#include "basicio.h"

//コンパイルオプション
#ifdef TWELITE_RED
#define DEFAULT_MY_ADDR         0x0200
#define DEFAULT_TARGET_ADDR     0x0201
#else
#define DEFAULT_MY_ADDR         0x0201
#define DEFAULT_TARGET_ADDR     0x0200
#endif

//基本設定
#define DEFAULT_APPID           0x55553201
#define BASE_CHANNEL            11      // +0～+3
#define BASE_ADDR               0x0200  // +0～+3
#define DEFAULT_BAUDRATE        SERIAL_BAUD_9600
#define SECOND_BAUDRATE         SERIAL_BAUD_38400

//DIO割り当て1
#define DIO_CH_B1               3
#define DIO_CH_B0               2
#define DIO_BAUDRATE            1
#define DIO_SLEEP               0
#define DIO_MY_ADDR_B1          17
#define DIO_MY_ADDR_B0          15
#define DIO_RADIO_TX            16  //変更不可(モノスティック依存)
#define DIO_SERIAL_ERR          13  //Error, HIGH->LOW
#define DIO_RADIO_ERR           12  //Error, HIGH->LOW

//DIO割り当て2
#define DIO_GPIO_SETUP          14
#define DIO_RTS                 5   //変更不可(TWELITE標準)
#define DIO_TGT_ADDR_B0         18
#define TMR_RADIO_RX            3   //DO1に接続のLEDはアナログ出力のためタイマーを使う
#define DIO_TGT_ADDR_B1         19
#define DIO_DEBUG_OUT           8
#define DIO_HW_FLOW_CTRL        10


//動作に関する定数
#define RADIO_TX_SIZE           104 //無線送信最大サイズ（ショートアドレス）
#define RADIO_TX_MAX_QUE        3   //無線送信キュー。ToCoNet_USE_MOD_TXRXQUEUE_xxx に依存する
#define RADIO_TX_RETRY          2   //再送信回数
#define RADIO_TX_RETRY2         1   //再再送信回数
#define SERIAL_RX_EMPTY_WAIT    2   //シリアル受信がないときで、受信バッファ残分を無線送信するまでの待ちカウント (x4ms)
#define SERIAL_RX_EMPTY_WAIT_S  1000 //シリアル受信がないときで、デバッグ時の送信統計を表示するまでの待ちカウント (x4ms)


//信号ON(LOW)時間(カウント、x4ms)
#define COUNT_RADIO_TX          25
#define COUNT_RADIO_RX          25
#define COUNT_RADIO_ERR         25
#define COUNT_SERIAL_ERR        25

//信号ON(LOW)時間をカウントダウン
uint8_t countRadioTx;
uint8_t countRadioRx;
uint8_t countRadioErr;
uint8_t countSerialErr;

//必要な設定値の記憶
bool_t bDebugOut;               //シリアルにデバッグ出力するか
uint16_t u16MyAddr;             //自分のショートアドレス
uint16_t u16TargetAddr;         //相手のショートアドレス
bool_t bHwFlowControl;          //ハードウェアフロー制御を行うか

//バッファ内にデータがあって、シリアルからの受信データがなくなってからのカウント
uint16_t serialNoRxCount;

//シリアルからの設定を受け付けるか
#define SOFT_SETTING_START_WORD         "+++WSS"
#define SOFT_SETTING_START_WORD_LENGTH  6
bool_t bAcceptFromSerialSetting;
bool_t bSerialSettingMode;

//ソフトウェア設定モードの構文解析と設定値を記憶（初期化しない）
int8_t wss_hwFlowControl;
int8_t wss_baudrate;
int8_t wss_channel;
int8_t wss_myAddr;
int8_t wss_targetAddr;
int8_t wss_debugOutput;
bool_t wss_hasError;
uint8_t currentItem;
int8_t requredDigit;


//統計
uint32_t radioSendCount;        //送信回数
uint32_t radioSendFailCount;    //送信失敗回数

//無線受信バッファ
#define RADIO_RX_BUF_COUNT (5 * RADIO_TX_SIZE)
uint8_t u8RadioRxBuf[RADIO_RX_BUF_COUNT];
BYTEQUE radioRxQue;


//コールバック関数
void dioSleepFunc();
void txFunc(uint8_t u8CbId,bool_t bSuccess);
void rxFunc(uint32_t u32SrcAddr,bool_t bBroadcast,uint8_t u8CbId,uint8_t u8DataType,uint8_t *pu8Data,uint8_t u8Length,uint8_t u8Lqi);

//シリアル受信、無線送信関数
void serialRx_init();
void serialRx_read();
void serialRx_send();
void serialRx_fixBalance();
void serialRx_sendResult(uint8_t u8CbId, bool_t bSuccess);

//ソフトウェア設定処理関数
void procSoftwareSettingCommands();


//変数初期化
void initVars() {
    countRadioTx = 0;
    countRadioRx = 0;
    countRadioErr = 0;
    countSerialErr = 0;
    countSerialErr = 0;

    serialNoRxCount = 0;
    radioSendCount = 0;
    radioSendFailCount = 0;

    bSerialSettingMode = FALSE;

    serialRx_init();
    que_init(&radioRxQue, u8RadioRxBuf, RADIO_RX_BUF_COUNT);
}

// 起動時や起床時に呼ばれる関数
void setup(bool_t warmWake, uint32_t bitmapWakeStatus) {

    initVars();

    /*
     * 初期値の取得
     */

    uint8_t ch = BASE_CHANNEL;
    uint8_t baud = DEFAULT_BAUDRATE;

    if(dio_read(DIO_GPIO_SETUP) == LOW) {
        //GPIOセットアップ

        //チャンネル
        ch = BASE_CHANNEL;
        if(dio_read(DIO_CH_B0) == LOW) ch += 1;
        if(dio_read(DIO_CH_B1) == LOW) ch += 2;

        //ボーレート
        baud = (dio_read(DIO_BAUDRATE) == LOW) ? SECOND_BAUDRATE : DEFAULT_BAUDRATE;

        //無線アドレス(自分)
        u16MyAddr = BASE_ADDR;
        if(dio_read(DIO_MY_ADDR_B0) == LOW) u16MyAddr += 1;
        if(dio_read(DIO_MY_ADDR_B1) == LOW) u16MyAddr += 2;

        //無線アドレス(相手)
        u16TargetAddr = BASE_ADDR;
        if(dio_read(DIO_TGT_ADDR_B0) == LOW) u16TargetAddr += 1;
        if(dio_read(DIO_TGT_ADDR_B1) == LOW) u16TargetAddr += 2;

        //デバッグ出力
        bDebugOut = (dio_read(DIO_DEBUG_OUT) == LOW);

        //ハードウェアフロー制御
        bHwFlowControl = (dio_read(DIO_HW_FLOW_CTRL) == LOW);

        //ソフトウェアセッティングは受け付けない
        bAcceptFromSerialSetting = FALSE;
    }
    else {
        //デフォルト値
    
        ch = BASE_CHANNEL;
        baud = DEFAULT_BAUDRATE;
        u16MyAddr = DEFAULT_MY_ADDR;
        u16TargetAddr = DEFAULT_TARGET_ADDR;
        bDebugOut = FALSE;
        bHwFlowControl = FALSE;

        //ソフトウェアセッティングを受け付ける
        bAcceptFromSerialSetting = TRUE;

        if (warmWake) {
            //ソフトウェアセッティング後の起床（再起動）

            //チャンネル
            if (wss_channel >= 0) {
                if (wss_channel & 1) ch += 1;
                if (wss_channel & 2) ch += 2;
            }

            //ボーレート
            if (wss_baudrate >= 0)
                    baud = wss_baudrate ? SECOND_BAUDRATE : DEFAULT_BAUDRATE;

            //無線アドレス(自分)
            if (wss_myAddr >= 0) {
                u16MyAddr = BASE_ADDR;
                if (wss_myAddr & 1) u16MyAddr += 1;
                if (wss_myAddr & 2) u16MyAddr += 2;
            }

            //無線アドレス(相手)
            if (wss_targetAddr >= 0) {
                u16TargetAddr = BASE_ADDR;
                if (wss_targetAddr & 1) u16TargetAddr += 1;
                if (wss_targetAddr & 2) u16TargetAddr += 2;
            }

            //デバッグ出力
            if (wss_debugOutput >= 0)
                    bDebugOut = (bool_t)wss_debugOutput;

            //ハードウェアフロー制御
            if (wss_hwFlowControl >= 0)
                    bHwFlowControl = (bool_t)wss_hwFlowControl;
        }
    }
    

    /*
     * シリアル初期化
     */

    serial_initEx(baud, SERIAL_PARITY_NONE, SERIAL_LENGTH_8BITS, SERIAL_STOP_1BIT,
                FALSE, bHwFlowControl ? SERIAL_HWFC_TIMER4 : SERIAL_HWFC_NONE);

    if (bDebugOut)
        serial_printf("\r\n[D] SERIAL:%ubps FLOW CONTROL:%s\r\n",
                (baud == SERIAL_BAUD_38400) ? 38400 : 9600,
                bHwFlowControl ? "Yes" : "No");


    /*
     * DIO初期化
     */

    //無線受信信号（モノスティックLED黄）
    timer_attachAnalogWrite(TMR_RADIO_RX, 0, DO_PIN);   //0 = LOW = 点灯
    countRadioRx = 125;                                 //125=0.5秒

    //無線送信信号（モノスティックLED赤）
    dio_pinMode(DIO_RADIO_TX, INPUT);                   //PULLUP OFF
    dio_write(DIO_RADIO_TX, LOW);                       //LOW = 点灯
    dio_pinMode(DIO_RADIO_TX, OUTPUT);
    countRadioTx = 125;                                 //125=0.5秒

    //無線送信エラー信号
    dio_pinMode(DIO_RADIO_ERR, INPUT);               //PULLUP OFF
    dio_write(DIO_RADIO_ERR, HIGH);
    dio_pinMode(DIO_RADIO_ERR, OUTPUT);              //HIGH = 消灯

    //シリアルエラー信号
    dio_pinMode(DIO_SERIAL_ERR, INPUT);              //PULLUP OFF
    dio_write(DIO_SERIAL_ERR, HIGH);
    dio_pinMode(DIO_SERIAL_ERR, OUTPUT);             //HIGH = 消灯

    //スリープ用DIO割り込みを設定
    dio_attachCallback(DIO_SLEEP, FALLING, dioSleepFunc);

    if (!bHwFlowControl) {
        //ハードウェアフロー制御を行わないときは、ソフトウェアでRTSを制御する
        //Arduinoなどのハードウェアフロー制御を持たないマイコン向け

        dio_pinMode(DIO_RTS, OUTPUT);                   //PULLUP OFF
        dio_write(DIO_RTS, LOW);
        dio_pinMode(DIO_RTS, OUTPUT);                   //LOW = シリアル送信許可
    }


    /*
     * 無線初期化
     */

    radio_setupInit(RADIO_MODE_TXRX, DEFAULT_APPID, ch, 3);
    radio_setupShortAddress(u16MyAddr);
    radio_attachCallback(txFunc, rxFunc);
    radio_setRetry(RADIO_TX_RETRY, 5);

    if (bDebugOut)
        serial_printf("\r\n[D] RADIO APP ID:0x%08x CHANNEL:%u MY ADDR:0x%04x TARGET ADDR:0x%04x\r\n",
                DEFAULT_APPID, ch, u16MyAddr, u16TargetAddr);

    if (u16MyAddr == u16TargetAddr) {
        //自分と相手のアドレスが同じなのでエラー

        //LED点灯
        dio_write(DIO_RADIO_ERR, LOW);               //点灯
        countRadioErr = 125;                          //125=0.5秒

        if (bDebugOut)
            serial_puts("\r\n[E] MY ADDR = TARGET ADDR !\r\n");
    }

}

// setup()後、３種類のイベントで呼ばれるループ関数
void loop(EVENTS event) {

    if (event == EVENT_START_UP) {
        // 最初に呼ばれる
    } else if (event == EVENT_TICK_SECOND) {
        // 1秒毎に呼ばれる
    } else if (event == EVENT_TICK_TIMER) {
        // 4ミリ秒毎(デフォルト)に呼ばれる

        /*
         * 出力信号のOFF処理
         */

        //無線送信信号
        if (countRadioTx > 0 && --countRadioTx == 0)
            dio_write(DIO_RADIO_TX, HIGH);

        //無線受信信号
        if (countRadioRx > 0 && --countRadioRx == 0)
            timer_updateAnalogPower(TMR_RADIO_RX, 65535);

        //無線送信エラー
        if (countRadioErr > 0 && --countRadioErr == 0)
            dio_write(DIO_RADIO_ERR, HIGH);

        //シリアルエラー
        if (countSerialErr > 0 && --countSerialErr == 0)
            dio_write(DIO_SERIAL_ERR, HIGH);
        

        /*
         * シリアル受信処理
         */

        if (bSerialSettingMode) {
            //ソフトウェア設定モード

            procSoftwareSettingCommands();
        }
        else if (serial_getRxCount() > 0) {

            serialNoRxCount = 0;

            //シリアル受信処理
            serialRx_read();

            //シリアルエラーのチェック
            if (serial_dataLost()) {
                dio_write(DIO_SERIAL_ERR, LOW);
                countSerialErr = COUNT_SERIAL_ERR;

                if (bDebugOut)
                    serial_puts("\r\n[E]SERIAL RX DATA LOST\r\n");
            }
        }
        else if (serialNoRxCount < SERIAL_RX_EMPTY_WAIT) {
            if (++serialNoRxCount == SERIAL_RX_EMPTY_WAIT) {

                //シリアル受信がなくなって一定時間が経過したので、
                //バッファ内の半端データも送信対象にする
                serialRx_fixBalance();
            }
        }
        else if (bDebugOut && serialNoRxCount < SERIAL_RX_EMPTY_WAIT_S) {
            if (++serialNoRxCount == SERIAL_RX_EMPTY_WAIT_S) {

                //これまでの送信結果を表示
                serial_printf("\r\n[D] RADIO TX TOTAL:%uPKT FAIL:%uPKT\r\n",
                        radioSendCount, radioSendFailCount);
            }
        }


        /*
         * 無線送信処理
         */

        serialRx_send();


        /*
         * シリアル送信処理
         */

        while (que_getCount(&radioRxQue) > 0 &&
                SERIAL_TX_BUFFER_SIZE > serial_getTxCount()) {
            
            serial_putc(que_get(&radioRxQue));
        }



    }
}

//DIO_SLEEPがLOWになったときに呼び出される
void dioSleepFunc() {

    //コールバックを停止
    dio_detach(DIO_SLEEP);

    //アナログ出力を停止
    timer_detach(TMR_RADIO_RX);

    //スリープ
    dio_setWake(DIO_SLEEP, RISING); //DIO_SLEEPがHIGHに戻ったら起床する
    sleep(FALSE, FALSE);
}

//無線送信コールバック関数
void txFunc(uint8_t u8CbId,bool_t bSuccess) {

    bAcceptFromSerialSetting = FALSE;

    //統計
    radioSendCount++;
    if (!bSuccess) radioSendFailCount++;

    serialRx_sendResult(u8CbId, bSuccess);
}

//無線受信コールバック関数
void rxFunc(uint32_t u32SrcAddr,bool_t bBroadcast,uint8_t u8CbId,
        uint8_t u8DataType,uint8_t *pu8Data,uint8_t u8Length,uint8_t u8Lqi) {

    //キューに読み込む
    uint8_t n = u8Length;
    while (n-- > 0)
        que_append(&radioRxQue, *pu8Data++);

    //LED点灯
    uint32_t pw = (uint32_t)(255 - u8Lqi) << 8;
    timer_updateAnalogPower(TMR_RADIO_RX, (pw > 65535) ? 65535 : 0);
    countRadioRx = COUNT_RADIO_RX;

    if (bDebugOut)
       serial_printf("\r\n[D] RADIO RX:%uBYTE ADDR:0x%04x ID:%u\r\n",
                        u8Length, u32SrcAddr, u8CbId);

    if (que_dataLost(&radioRxQue)) {
        //キューでデータが溢れた

        //エラー信号をたてる
        //ここで溢れる＝シリアルの問題
        dio_write(DIO_SERIAL_ERR, LOW);
        countSerialErr = COUNT_SERIAL_ERR;

        if (bDebugOut)
            serial_puts("\r\n[E]SERIAL TX DATA LOST\r\n");
    }
}



//シリアル受信バッファ

#define SERIAL_RX_BUF_COUNT 5

typedef enum {
    SRXB_EMPTY,
    SRXB_ADDING,
    SRXB_WAIT_FOR_SEND,
    SRXB_WAIT_FOR_RESULT
} SERIALRXSTATUS;

typedef struct {
    uint8_t u8Status;               //SERIALRXSTATUS値
    uint8_t u8Retry;                //再送回数
    uint8_t u8Order;                //送信順番
    uint8_t u8CbId;                 //無線送信ID
    uint8_t u8Length;               //データ長
    uint8_t u8Buf[RADIO_TX_SIZE];
} SERIALRXBUFFER;

//バッファの実体
SERIALRXBUFFER serialRxBuffer[SERIAL_RX_BUF_COUNT];

//書き込み中のバッファ(未定の場合はNULL)
SERIALRXBUFFER *pSerialRxBufferAdding;

//送信順番番号を管理
uint8_t serialRxBufferOrderNo;

//次の送信番号
uint8_t serialRxBufferNextTxNo;

//送信結果待ちがある
bool_t serialRxBufferWaitForResult;

//使用中のバッファ数
int8_t serialRxBufferCount;

void serialRx_init() {
    uint8_t i;
    for (i=0; i<SERIAL_RX_BUF_COUNT; i++)
        serialRxBuffer[i].u8Status = SRXB_EMPTY;
    pSerialRxBufferAdding = NULL;
    serialRxBufferOrderNo = 0;
    serialRxBufferNextTxNo = 0;
    serialRxBufferWaitForResult = FALSE;
    serialRxBufferCount = 0;
}

void serialRx_read() {
    uint16_t count = serial_getRxCount();

    while (count > 0) {
        if (pSerialRxBufferAdding == NULL) {

            //データを保存するバッファを決定する
            SERIALRXBUFFER *p = serialRxBuffer;
            uint8_t i;
            for (i=0; i<SERIAL_RX_BUF_COUNT; i++) { //空のバッファを探すループ
                if (p->u8Status == SRXB_EMPTY) {
                    pSerialRxBufferAdding = p;
                    pSerialRxBufferAdding->u8Status = SRXB_ADDING;
                    pSerialRxBufferAdding->u8Length = 0;
                    serialRxBufferCount++;
                    break;
                }
                p++;
            }

            //バッファに空きが無い
            //シリアル受信バッファがいっぱいになれば、受信を停止（フロー制御あり）するか、
            //バッファオーバーフロー（フロー制御なし）でエラー信号が出力されるだろう
            if (pSerialRxBufferAdding == NULL) {

                if (!bHwFlowControl) {
                    //ハードウェアフロー制御を行わないときは、ソフトウェアでRTSを制御する
                    //ここでRTSをOFFしても、相手の送信バッファに入ってくる分は送られてくる。

                    dio_write(DIO_RTS, HIGH);   //シリアル送信禁止
                }

                return;
            }
        }

        while (count > 0) {

            //シリアルからバッファに読み込む
            pSerialRxBufferAdding->u8Buf[pSerialRxBufferAdding->u8Length]
                     = (uint8_t)serial_getc();
            count--;
            pSerialRxBufferAdding->u8Length++;


            //ソフトウェア設定モードに入るためのチェック
            if (bAcceptFromSerialSetting &&
                pSerialRxBufferAdding->u8Length == SOFT_SETTING_START_WORD_LENGTH) {

                bAcceptFromSerialSetting = FALSE;

                if (strncmp(pSerialRxBufferAdding->u8Buf, SOFT_SETTING_START_WORD,
                    SOFT_SETTING_START_WORD_LENGTH) == 0) {

                    //既定のワードが入力されたのでソフトウェア設定モードに入る
                    bSerialSettingMode = TRUE;
                    pSerialRxBufferAdding->u8Length = 0;

                    //変数初期化
                    wss_hwFlowControl = -1;
                    wss_baudrate = -1;
                    wss_channel = -1;
                    wss_myAddr = -1;
                    wss_targetAddr = -1;
                    wss_debugOutput = -1;
                    wss_hasError = FALSE;
                    currentItem = 0;
                    requredDigit = 0;
                    return;
                }
            }

            if (pSerialRxBufferAdding->u8Length == RADIO_TX_SIZE) {
                //バッファがいっぱいになった

                //送信待ちにステータスを変更
                pSerialRxBufferAdding->u8Status = SRXB_WAIT_FOR_SEND;
                pSerialRxBufferAdding->u8Order = serialRxBufferOrderNo++;
                pSerialRxBufferAdding->u8Retry = 0;
                pSerialRxBufferAdding = NULL;
                break;
            }
        }
    }
}

void serialRx_send() {

    if (serialRxBufferWaitForResult)
        //送信結果待ちのデータがある場合は送信できない
        return;

    uint8_t i;
    for (i=0; i<SERIAL_RX_BUF_COUNT; i++) {

        if (serialRxBuffer[i].u8Status == SRXB_WAIT_FOR_SEND &&
            serialRxBuffer[i].u8Order == serialRxBufferNextTxNo) {
            
            //送信
            int16_t res = radio_write(u16TargetAddr, 0,
                            serialRxBuffer[i].u8Buf, serialRxBuffer[i].u8Length);

            if (res != -1) {
                //送信した（成功かどうかはまだ）

                //送信信号をたてる
                dio_write(DIO_RADIO_TX, LOW);
                countRadioTx = COUNT_RADIO_TX;

                //ステータスを更新する
                serialRxBuffer[i].u8Status = SRXB_WAIT_FOR_RESULT;
                serialRxBuffer[i].u8CbId = (uint8_t)res;

                if (bDebugOut) {
                    if (serialRxBuffer[i].u8Retry == 0)
                        serial_printf("\r\n[D] RADIO TX:%uBYTE ID:%u\r\n",
                                serialRxBuffer[i].u8Length, res);
                    else
                        serial_printf("\r\n[D] RADIO TX:%uBYTE RETRY:%u ID:%u\r\n",
                                serialRxBuffer[i].u8Length, serialRxBuffer[i].u8Retry, res);
                }
            } else {
                //送信エラー（考えられるのは自分と相手のアドレスが同じ場合くらい）

                //エラー信号をたてる
                dio_write(DIO_RADIO_ERR, LOW);
                countRadioErr = COUNT_RADIO_ERR;

                //データは破棄する
                serialRxBuffer[i].u8Status = SRXB_EMPTY;
                serialRxBufferNextTxNo++;
                serialRxBufferCount--;

                if (!bHwFlowControl && serialRxBufferCount <= (SERIAL_RX_BUF_COUNT / 2)) {
                    //ハードウェアフロー制御を行わないときは、ソフトウェアでRTSを制御する

                    dio_write(DIO_RTS, LOW);    //シリアル送信許可
                }

                if (bDebugOut)
                    serial_printf("\r\n[E] RADIO TX:%ubyte\r\n",
                                    serialRxBuffer[i].u8Length);
            }

            return;
        }
    }
}

//バッファに読みかけのデータを送信対象にする
void serialRx_fixBalance() {
    if (pSerialRxBufferAdding != NULL &&
        pSerialRxBufferAdding->u8Status == SRXB_ADDING &&
        pSerialRxBufferAdding->u8Length > 0) {

        //ソフトウェア設定モードに入りそうでなければ送信対象にする
        if (!bAcceptFromSerialSetting ||
            strncmp(pSerialRxBufferAdding->u8Buf, SOFT_SETTING_START_WORD,
                    pSerialRxBufferAdding->u8Length) != 0) {

            pSerialRxBufferAdding->u8Status = SRXB_WAIT_FOR_SEND;
            pSerialRxBufferAdding->u8Retry = 0;
            pSerialRxBufferAdding->u8Order = serialRxBufferOrderNo++;
            pSerialRxBufferAdding = NULL;
        }
    }
}

void serialRx_sendResult(uint8_t u8CbId, bool_t bSuccess) {
    uint8_t i;
    for (i=0; i<SERIAL_RX_BUF_COUNT; i++) {
        if (serialRxBuffer[i].u8Status  == SRXB_WAIT_FOR_RESULT &&
            serialRxBuffer[i].u8CbId == u8CbId) {

            serialRxBufferWaitForResult = FALSE;

            if (bSuccess) {
                //送信完了したのでバッファを空にする
                serialRxBuffer[i].u8Status = SRXB_EMPTY;
                serialRxBufferNextTxNo++;
                serialRxBufferCount--;

                if (!bHwFlowControl && serialRxBufferCount <= (SERIAL_RX_BUF_COUNT / 2)) {
                    //ハードウェアフロー制御を行わないときは、ソフトウェアでRTSを制御する

                    dio_write(DIO_RTS, LOW);    //シリアル送信許可
                }

                if (bDebugOut)
                    serial_printf("\r\n[D] RADIO TX ID:%u SUCCESS\r\n",
                                    u8CbId);
                break;
            }

            //送信失敗
            if (serialRxBuffer[i].u8Retry < RADIO_TX_RETRY2) {
                //再送信対象にする
                serialRxBuffer[i].u8Status = SRXB_WAIT_FOR_SEND;
                serialRxBuffer[i].u8Retry++;

                if (bDebugOut)
                    serial_printf("\r\n[D] RADIO TX ID:%u FAIL\r\n",
                                    u8CbId);

                break;
            }

            //再送信失敗、データを破棄
            serialRxBuffer[i].u8Status = SRXB_EMPTY;
            serialRxBufferNextTxNo++;
            serialRxBufferCount--;

            if (!bHwFlowControl && serialRxBufferCount <= (SERIAL_RX_BUF_COUNT / 2)) {
                //ハードウェアフロー制御を行わないときは、ソフトウェアでRTSを制御する

                dio_write(DIO_RTS, LOW);    //シリアル送信許可
            }

            //エラー信号をたてる
            dio_write(DIO_RADIO_ERR, LOW);
            countRadioErr = COUNT_RADIO_ERR;

            if (bDebugOut)
                serial_printf("\r\n[E] RADIO TX:%uBYTE ID:%u NOT REACHED\r\n",
                                serialRxBuffer[i].u8Length, u8CbId);
            break;
        }
    }
}

void procSoftwareSettingCommands() {
    while (serial_getRxCount()) {

        uint8_t c = (uint8_t)serial_getc();
        if (c == '-') {
            if (requredDigit != 0) {
                //残桁数は0でないといけない
                wss_hasError = TRUE;
            }

            bSerialSettingMode = FALSE;

            if (!wss_hasError) {
                //エラーが無ければ変数を保持したまま再起動する
                sleepTimer(10, TRUE);
            }
            else if (bDebugOut) {
                serial_puts("\r\n[E] SOFTWARE SETTING PARAM ERROR\r\n");
            }
            return;
        }
        else if (c == '0') {
            if (requredDigit <= 0) {
                //想定しない桁数
                wss_hasError = TRUE;
            } else {
                requredDigit--;
                switch (currentItem) {
                case 'H': wss_hwFlowControl = 0; break;
                case 'B': wss_baudrate = 0; break;
                case 'C': wss_channel <<= 1; break;
                case 'M': wss_myAddr <<= 1; break;
                case 'T': wss_targetAddr <<= 1; break;
                case 'D': wss_debugOutput = 0; break;
                default:
                    //想定しない項目
                    wss_hasError = TRUE;
                }
            }
        }
        else if (c == '1') {
            if (requredDigit <= 0) {
                //想定しない桁数
                wss_hasError = TRUE;
            } else {
                requredDigit--;
                switch (currentItem) {
                case 'H': wss_hwFlowControl = 1; break;
                case 'B': wss_baudrate = 1; break;
                case 'C': wss_channel <<= 1; wss_channel++; break;
                case 'M': wss_myAddr <<= 1; wss_myAddr++; break;
                case 'T': wss_targetAddr <<= 1; wss_targetAddr++; break;
                case 'D': wss_debugOutput = 1; break;
                default:
                    //想定しない項目
                    wss_hasError = TRUE;
                }
            }
        }
        else {
            if (requredDigit != 0) {
                //残桁数は0でないといけない
                wss_hasError = TRUE;
            }
            else {
                switch (c) {
                case 'H':
                    currentItem = c;
                    requredDigit = 1;
                    break;
                case 'B':
                    currentItem = c;
                    requredDigit = 1;
                    break;
                case 'C':
                    currentItem = c;
                    wss_channel = 0;
                    requredDigit = 2;
                    break;
                case 'M':
                    currentItem = c;
                    wss_myAddr = 0;
                    requredDigit = 2;
                    break;
                case 'T':
                    currentItem = c;
                    wss_targetAddr = 0;
                    requredDigit = 2;
                    break;
                case 'D':
                    currentItem = c;
                    requredDigit = 1;
                    break;
                default:
                    //想定しない項目
                    wss_hasError = TRUE;
                }
            }
        }
    }
}

