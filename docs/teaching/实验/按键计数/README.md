# 按键计数

这个独立的 C11 实验在 WSL 或任意支持 C11 的电脑上可运行，无需连接开发板。

~~~bash
cc -std=c11 -Wall -Wextra -Werror counter.c test_counter.c -o counter_test
./counter_test
~~~

通过会显示 PASS。阅读 counter_update 如何识别按下边沿。把计数上限传为 5，补上达到上限后再点的用例。进阶作业是把独立状态接到木鱼例程的事件回调，并用回读值更新标签。

模拟输入假定已去抖。真实机械按键的去抖应放在输入层或采样层。清零时若按键仍按着，要等释放后才算一次新敲击。
