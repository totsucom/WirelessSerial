
//受信スロットを初期化する
//相手から通信開始を受け取った
//次に受信するであろうCbId値を渡す。相手からの通信開始データのCbId値+1
void radioRx_init(uint8_t nextCbId);

//次にシリアルに書き出すCbId値
//保護モードで通信開始後のみ有効
uint8_t radioRx_getNextCbId();

//受信したユーザーデータを渡す
//無線受信コールバックから呼び出される
void radioRx_add(uint8_t cbId, uint8_t dataType, uint8_t *pData, uint8_t len);

//受信スロット内のデータを処理してシリアルに書き出す
void radioRx_sendToSerial();
