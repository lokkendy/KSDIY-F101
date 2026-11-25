# F101 Mac 選單列面板

這是以 macOS SwiftUI 與 Python 實作的 F101 選單列原型，啟動時預設為模擬模式。區網遙測、相機、暫停與停止均未經真機驗證。

## 建置與啟動

在 `F101Mac/` 執行。需要 macOS、Apple Command Line Tools 提供的 Swift 編譯器及 `/usr/bin/python3`。

```sh
/usr/bin/python3 -B -m unittest -v test_link
./build.sh
codesign --verify --strict 'build/F101 Panel.app'
open 'build/F101 Panel.app'
```

從選單列的打印機圖示開啟面板。首頁顯示狀態、進度、剩餘時間及溫度。底欄可切換分頁，從「控制」進入控制台，從「即時監控」進入相機。

## 功能與限制

- 模擬模式使用記憶體中的範例資料與靜態相機測試圖。移軸、回零、調平、進退料、溫度、速度、風扇、檔案、耗材與打印記錄等只提供模擬或佔位介面。
- 區網連線設定需要私有 IPv4、設備序號、LAN 存取碼及從可信管道取得的設備 TLS 憑證 SHA-256 指紋。設定只保存在執行中的程序記憶體。MQTT 與 JPEG 連線各自驗證憑證，驗證成功後才送出存取碼。
- 區網模式預期可顯示 MQTT 遙測，並使用 P1／A1 的 JPEG 相機協定。區網暫停與停止需確認，且僅在設備最近回報的狀態允許時送出。機型、韌體、相機畫面及實際控制結果均待真機驗證。RTSP、FTPS 與雲端功能尚未接入。

## 交接與驗證

`Panel.swift` 管理選單列、模型與區網子程序，`ControlPage.swift` 和 `Navigation.swift` 實作頁面。 `link.py` 處理 MQTT、JPEG 與 TLS，`test_link.py` 使用本機 socketpair 與臨時測試憑證驗證協定，`build.sh` 產生並臨時簽署本機 App。

五項本機測試涵蓋指令順序、設備狀態過期與撤銷、JPEG 影格、TLS 正確指紋及錯誤指紋拒絕。原型已在 macOS 模擬模式操作驗證主要分頁、控制、暫停及停止確認。此分支新增 Mac 源碼、測試、建置腳本及本文件。

## 原作與來源

感謝 KSDIY 酷世 DIY 與 Kevincoooool。原型的版面、導覽及協定參考 [Kevincoooool/KSDIY-F101 的拓竹屏原始碼](https://github.com/Kevincoooool/KSDIY-F101/tree/623fb75f7d6c848bc460547c9e0e7b8a6ac5dac5/firmware/bambu-panel)，包含 `HomeView.c`、`Control.c`、`ui_theme.c` 與 `bambu_camera.c`。Mac 目錄使用系統字型與 SF Symbols，未打包上游韌體、圖片、字型或第三方 codec。指定的上游基準未找到整體授權文件。原作者及第三方權利依各自適用條款保留。
