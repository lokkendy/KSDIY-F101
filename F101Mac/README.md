# F101 Mac 選單列面板

在 macOS 選單列查看 Bambu 打印任務、進度與溫度，點圖示可開啟設備頁或切到相機。可先用模擬模式體驗各頁與打印失敗通知。

## 啟動

在已安裝 Swift 編譯器、codesign 及系統 Python 3 的 Mac 上，從專案根目錄執行：

```sh
cd F101Mac
./build.sh
open "build/F101 Panel.app"
```

開啟後點選單列的面板圖示。從區網連線設定輸入打印機的私有 IPv4、序號、LAN 存取碼，以及透過可信管道取得的設備 TLS 憑證 SHA-256 指紋。連線設定僅保留在程序記憶體，重開後須重新輸入。監控使用 MQTT 8883，相機使用 JPEG 6000；兩者各自核對設備憑證。

## 操作與驗證範圍

- 已在簽名的 v3 App 從選單列完成真機唯讀驗證。設備頁顯示 A1 的打印狀態、進度、噴頭與熱床溫度，相機頁顯示即時畫面。
- 區網控制限暫停與停止，均需在面板確認。兩項動作已通過模擬與協定測試，真機控制尚未驗證，專用試印暫緩。
- 移軸、回零、調溫、進退料、風扇、速度與繼續打印僅供模擬操作。區網模式的層數、已用時間及預計完成時刻尚無實機資料。
- 打印失敗通知需要 macOS 通知權限。可在模擬模式使用「模擬打印失敗」體驗通知與點擊返回設備頁。

## 打印記錄

此版記錄頁使用模擬資料。規劃中的本機模式會保存 Mac 在線期間收到的任務事件。規劃中的 API 模式可查回伺服器已記錄且仍可查詢的任務，需要另外運行 [Bambu Panel](https://github.com/lokkendy/bambu-panel) 伺服器。

## 原作與來源
感謝 KSDIY 酷世 DIY 與 Kevincoooool。原型的版面、導覽及協定參考 [Kevincoooool/KSDIY-F101 的拓竹屏原始碼](https://github.com/Kevincoooool/KSDIY-F101/tree/623fb75f7d6c848bc460547c9e0e7b8a6ac5dac5/firmware/bambu-panel)，包含 `HomeView.c`、`Control.c`、`ui_theme.c` 與 `bambu_camera.c`。Mac 目錄使用系統字型與 SF Symbols，未打包上游韌體、圖片、字型或第三方 codec。指定的上游基準未找到整體授權文件。原作者及第三方權利依各自適用條款保留。
