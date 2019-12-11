
//シグナル出力ピン
#define PIN_REQUEST_TO_SEND         5
#define PIN_RADIO_TX                16
#define TMR_RADIO_RX                3       //DO1タイマー
#define PIN_ERROR_TX                13
#define PIN_ERROR_RX                12

//シグナル出力の初期化
//setting_init()の後に呼び出すこと
void signal_init();

//シグナル出力のリセット（通信開始時）
void signal_reset();

//EVENT_TICK_TIMER 毎に呼び出すこと
void signal_update();

//HWフロー制御を行わない場合で、TRUEを渡すと送信許可(LOW)になり、
//FALSEを渡すと送信禁止(HIGH)になる
//出力が変化したときにTRUEを返す
bool_t signal_requestToSend(bool_t b);

//無線送信したときに呼び出す
void signal_radioTx();

//無線受信したときに呼び出す
void signal_radioRx();

//無線信号品質を表すLQI値を渡す
void signal_setLqi(uint8_t lqi);

//シリアル受信～無線送信でエラーやデータ欠落があった場合に呼び出す
void signal_txError();

//無線受信～シリアル送信でエラーやデータ欠落があった場合に呼び出す
void signal_rxError();
