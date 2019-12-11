
//モジュール変数の初期化
//setting_load() 後に呼び出すこと
void radioTx_init();

//次に送信するCbId値を返す
uint8_t radioTx_getNextCbId();

//シリアルからデータを読んで送信スロットに格納する
//設定モードへ入るワードを受けた場合はTRUEを返す
bool_t radioTx_readFromSerial(uint16_t bytesToRead);

//書き込み中のスロットがあれば送信対象にする
void radioTx_sendBalance();

//ビーコンパケットを送信する
void radioTx_sendBeacon();

//通信開始パケットを送信する
uint8_t radioTx_sendConnectionStart(bool_t bReply);

//送信スロットの内の優先順位に応じて無線送信を行う
void radioTx_send();

//ユーザーデータの無線送信結果を渡す
void radioTx_sendResult(uint8_t u8CbId, bool_t bSuccess);

