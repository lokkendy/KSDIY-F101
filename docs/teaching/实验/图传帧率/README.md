# 图传帧率

此样本来自 KSDIY F101S3 Action 6 R16 横屏测试，已去除设备标识及凭据。运行：

~~~text
python analyze_status.py status_sample.json --output result.json
~~~

Windows 可用 py -3 代替 python。预期整段约 60.188 秒，解码 29.97 fps、绘制 21.75 fps、翻页约 21.73 fps。图传 fps 由相邻的累计计数差除以单调时间差求得。

复制样本做第二轮，把中间一条的 drawn 累计值改为 0，核对排除区间；也可换一条重复时间戳。保留原始样本。连接状态、计数回退和采样窗口变化都要记录。分析器只读取输入，结果不含原始文本和设备身份。
