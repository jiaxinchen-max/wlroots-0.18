# 调试期间标记为unused的函数记录

## 目的
在调试tinywl启动问题时，临时禁用了输入处理相关的代码，导致一些函数未被使用。
为了避免编译错误，这些函数被标记为`__attribute__((unused))`。
调试完成后，需要根据此记录恢复这些函数的正常使用。

## 修改记录

### 1. tinywl/tinywl.c 中的cursor处理函数
- **server_cursor_motion** (行号: ~483)
  - 状态: 添加了 `__attribute__((unused))`
  - 原因: cursor事件监听器注册被注释掉
  - 恢复方法: 移除 `__attribute__((unused))`，恢复事件监听器注册

- **server_cursor_motion_absolute** (行号: ~499)
  - 状态: 添加了 `__attribute__((unused))`
  - 原因: cursor事件监听器注册被注释掉
  - 恢复方法: 移除 `__attribute__((unused))`，恢复事件监听器注册

- **server_cursor_button** (行号: ~515)
  - 状态: 添加了 `__attribute__((unused))`
  - 原因: cursor事件监听器注册被注释掉
  - 恢复方法: 移除 `__attribute__((unused))`，恢复事件监听器注册

- **server_cursor_axis** (行号: ~537)
  - 状态: 添加了 `__attribute__((unused))`
  - 原因: cursor事件监听器注册被注释掉
  - 恢复方法: 移除 `__attribute__((unused))`，恢复事件监听器注册

- **server_cursor_frame** (行号: ~549)
  - 状态: 添加了 `__attribute__((unused))`
  - 原因: cursor事件监听器注册被注释掉
  - 恢复方法: 移除 `__attribute__((unused))`，恢复事件监听器注册

### 2. backend/termux/backend.c 中的修改
- **backend_start** 函数中的 `termux_input_create_devices(backend)` 调用被注释掉
  - 状态: 注释掉了输入设备创建
  - 恢复方法: 取消注释 `termux_input_create_devices(backend);`

### 3. tinywl/tinywl.c 中的事件监听器注册
在main函数中，以下事件监听器注册被注释掉：
```c
// server.cursor_motion.notify = server_cursor_motion;
// wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
// server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
// wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
// server.cursor_button.notify = server_cursor_button;
// wl_signal_add(&server.cursor->events.button, &server.cursor_button);
// server.cursor_axis.notify = server_cursor_axis;
// wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
// server.cursor_frame.notify = server_cursor_frame;
// wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);
```

## 恢复步骤
1. 移除所有函数前的 `__attribute__((unused))`
2. 恢复 tinywl.c 中被注释的事件监听器注册代码
3. 恢复 backend.c 中的 `termux_input_create_devices(backend)` 调用
4. 重新编译测试

## 测试状态
- [ ] 输出功能测试（当前阶段）
- [ ] 输入功能测试
- [ ] 完整功能测试

---
创建时间: 2026-02-22
目的: 调试tinywl在termux环境下的启动问题