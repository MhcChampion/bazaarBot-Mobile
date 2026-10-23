# 代码级静态审查（BPP Mobile）

> 目的：在无法使用真机的前提下，把所有**可能静默出错**的假设列出来，并说明它们各自是怎么被
> 验证或兜底的。每一节都对应 `native/` 里的具体代码和 `tools/` 里的检查器。

---

## 1. 注入层

| 方案 | 做法 | 验证 | 失败表现 |
| --- | --- | --- | --- |
| 默认 | 给 `libil2cpp.so` 追加 `DT_NEEDED=libbpp.so`（LIEF） | `verify_elf.py --expect-needed` | 游戏启动即崩，`bpp.log` 不存在 |
| 备用 | 给 `libmain.so` 追加 `DT_NEEDED`（只改 6.7 KB） | 同上 | 同上 |
| 逃生 | `files/bpp_safe_mode` 标记文件 | `SafeModeRequested()` 在构造函数里最先检查 | 不装任何 hook，游戏按原版跑 |

**为什么要两个方案**：`libil2cpp.so` 有 148 MB、被 Unity 深度依赖；`libmain.so` 只有 6.7 KB，
是 Java 层 `System.loadLibrary("main")` 的入口。如果默认版崩、备用版能起，就能确定问题在
"改大库"而不是 mod 本身。

**加载顺序**：`libbpp` 作为 `DT_NEEDED` 会先于宿主库的构造函数运行，因此 `main.cpp` 的
`BootThread` 里**不**主动 `dlopen("libil2cpp.so")`，而是轮询
`dlsym(RTLD_DEFAULT, "il2cpp_domain_get")`，等到宿主库被游戏自己加载后再用
`RTLD_NOLOAD` 取句柄——避免与 linker 抢加载锁。

---

## 2. Inline hook 清单与 ABI

IL2CPP 把**实例方法**编译为：

```c
Ret Method(Class* __this, const MethodInfo* method, <params...>)
```

所以 C++ hook 的参数个数必须是 `托管参数个数 + 2`；托管返回非 `void` 时，hook 必须返回
`void*` 并把原函数返回值带回去（v0.2 曾因为漏了这一点导致开 tooltip 必崩）。

| 目标方法 | 托管签名（dump.cs） | C++ hook | ABI 检查 |
| --- | --- | --- | --- |
| `EventSystem.Update`（虚方法，RVA 0x82F9EA8） | `void Update()` | `void TickHook(void* self, void* mi)` | 0 参数 + 2 ✓ |
| `CardTooltipData.RenderTooltip`（私有，RVA 0x74DB…） | `string RenderTooltip(TooltipBuilder)` | `void* RenderTooltipHook(void* self, void* mi, void* builder)` | 1 参数 + 2，返回 `void*` ✓ |
| `eglSwapBuffers` | `EGLBoolean(EGLDisplay, EGLSurface)` | `HookSwap` | C ABI，2 参数 ✓ |
| `eglSwapBuffersWithDamageKHR/EXT` | `EGLBoolean(EGLDisplay, EGLSurface, const EGLint*, EGLint)` | `HookSwapDamage` | 4 参数 ✓ |
| `eglGetProcAddress` | `void*(const char*)` | `HookGetProcAddress` | 1 参数 ✓ |

以上由 `tools/check_hooks.py` 自动比对（表驱动，从 `dump.cs` 解析真实签名）。

**每帧 tick 的候选链**（任一命中即可）：`EventSystem.Update` → `StandaloneInputModule.Process`
→ `Camera.Render` → `Application.Update`。全部失败会打 `no per-frame hook candidate found`，
此时 mod 只剩 EGL 渲染、没有数据刷新。

**`MethodInfo.methodPointer` 的布局**：Unity 2021.2+ 是
`{ methodPointer; virtualMethodPointer; ... }`。`main.cpp` 不盲信偏移，而是分别尝试 `+0` 与 `+8`，
用 `dladdr()` 确认地址落在 `libil2cpp.so` 里才使用。

---

## 3. 反射调用约定

| 约定 | 代码位置 | 验证手段 |
| --- | --- | --- |
| `il2cpp_runtime_invoke` 对值类型返回值做装箱，数据在 `obj + object_header_size` | `game_api.cpp: BoxedI32`、`features.cpp: BoxedInt` | 首次读 touchCount 时打印 `boxedClass=System.Int32` |
| 值类型参数以指针传入（`int idx` 取地址） | `il2::Invoke1(M_GetTouch, nullptr, &idx)` | 同上日志（touchCount > 0 说明调用成功） |
| 所有托管调用都在 Unity 主线程（tick 内） | `features::Tick` | 没有跨线程调用 |
| 跨帧持有托管对象必须用 GC handle | `game_api.cpp: g_tooltip_handle` | tooltip 数据用 `gchandle_new`，每次替换前 free |
| 数组数据起始 = `array_data_offset()` | `il2cpp_api.cpp: ArrayData` | 运行时查询并打印 `layout: array data = 0x20` |
| 数组元素步长 = `il2cpp_array_element_size()` | `il2cpp_api.cpp: ArrayAt` | 运行时 API（v0.11 修的 bug：原来用 `class_value_size`，对引用类型数组完全错） |
| 对象头大小 = `il2cpp_object_header_size()` | `il2cpp_api.cpp: ObjectHeaderSize` | 运行时查询并打印 |

---

## 4. 硬编码假设清单

| 假设 | 值 | 验证方式 | 漂移后果 |
| --- | --- | --- | --- |
| `CardController.<CardData>` 偏移 | 0x90 | `check_offsets.py` ← dump.cs | 读到错误的 Card 指针 |
| `Card.TemplateId / Size / Tier` | 0x18 / 0x40 / 0x50 | 同上 | 卡牌张冠李戴 |
| `CardTooltipData._cardTemplate` | 0x18 | 同上 | tooltip 显示错误卡 |
| `UnityEngine.Touch.m_Position / m_Phase` | 0x04 / 0x24 | 同上 | 触摸坐标错乱 |
| `ClientBoardModel.bazaarBoardModel` | 字段名（无偏移） | `check_lookups.py` | 阵容退回 UI 扫描 |
| 类命名空间（`TheBazaar.Data`、`TheBazaar.Tooltips.CardTooltipData` …） | — | `check_lookups.py`（17 处） | 返回 null，功能静默失效 |
| 游戏符号存在性（32 个方法名） | — | `selfcheck.py` | 同上 |

---

## 5. 已修复缺陷时间线（都来自静态审查，而非真机）

| 版本 | 缺陷 | 后果 |
| --- | --- | --- |
| v0.3 | tooltip hook 返回 `void` 却对应 `string` 方法 | 一开提示就崩 |
| v0.4 | `KlassAny("", Name)` 只查空命名空间 | 所有带命名空间的类查不到，面板永远空 |
| v0.5 | 字段偏移裸写 | 无校验，游戏更新即错位 |
| v0.6 | 构造函数里 `dlopen` 宿主库 | 与 linker 抢锁 |
| v0.6 | `AddComponent<CanvasGroup>` 忽略已存在组件 | 打开面板时游戏仍可点击 |
| v0.8 | 只 hook `eglSwapBuffers` | Unity 走 `WithDamage` 时悬浮层不渲染 |
| v0.11 | 数组步长用 `class_value_size` | 引用类型数组读到错误地址 |
| v0.12 | 对象头/数组偏移写死 | 布局变化即全线错位 |

---

## 5.5 崩溃归因

`crash_guard.cpp` 装了 SIGSEGV/SIGBUS/SIGABRT/SIGILL/SIGFPE 处理器：

- 崩溃瞬间用 `write(2)` 往 `bpp.log` 追加一行：信号、`si_addr`、`pc`、`hook_depth`、当前 hook 名
- 然后**恢复安装前保存的旧处理器**（Unity 自带的崩溃上报会照常运行）并 `raise`
- 两个 hook 入口用 RAII `HookScope` 包裹，因此 `hook_depth>0` 即可判定崩溃发生在 mod 调用链内
- 全部动作只用 async-signal-safe 的原语（预开 fd + `write`），不调用 `dladdr`/`malloc`

## 5.6 图形后端的未知量

`libunity.so` 同时包含 GLES 与 Vulkan 两条渲染路径（字符串里有 `libvulkan.so`、
`Vulkan detection: %d` 等），最终用哪条由 Unity 的 Auto Graphics API 在启动时决定。

- 走 GLES：`eglSwapBuffers` 被调用 → `HookSwap` 生效 → 洋红探针 + ImGui 都能画
- 走 Vulkan：`eglSwapBuffers` **不会**被调用 → 探针与 ImGui 都不会出现，但 tick、数据、
  文件命令通道全部正常

因此 v0.15 增加了 `gfx: ticks=… egl_swaps=… vulkan_loaded=…` 自检行；在拿到这行之前，
不能断言"悬浮层不出现"是 hook 的问题。

与之对应，v0.16 起 **tooltip 增强走"改字符串"而不是"加图层"**：
`CardTooltipData::RenderTooltip` 返回的 `string` 被
`System.String::Concat(string,string)` 追加 BPP 信息，再由游戏自己的 UI 渲染。
这条路在 GLES / Vulkan 下行为一致，是目前唯一"渲染无关"的可见功能。

## 6. 只能靠真机回答的问题

1. bionic 是否接受 LIEF 改写后的 `DT_NEEDED`（日志第一行 `libbpp constructor`）
2. `il2cpp_runtime_invoke` 装箱约定是否与假设一致（日志 `boxedClass=System.Int32`）
3. Unity 6 的 `FindObjectsOfType` 返回是否包含失活对象（决定阵容是否缺卡）
4. `Data.IsManagerCreated()` 何时变真（决定图鉴何时有数据）
5. ImGui + GLES3 在目标机型上的上下文兼容性（洋红探针 + `gl canary drawn`）

对应地，`bpp.log` 里这五行就是判定依据：

```
libbpp constructor
layout: object header = 0x10 / layout: array data = 0x20
tick hook installed: EventSystem::Update
gl canary drawn 240x60
catalog built: N cards
```

---

## 7. 工具矩阵

| 工具 | 作用 | 何时跑 |
| --- | --- | --- |
| `tools/check_lookups.py` | 类查找的命名空间+类名 vs dump | 每次改反射代码 |
| `tools/check_offsets.py` | 字段偏移常量 vs dump | 每次改数据读取 |
| `tools/check_hooks.py` | hook 的 C++ 签名 vs 托管签名 | 每次加/改 hook |
| `tools/selfcheck.py` | 32 个游戏符号存在性 | 每次构建 |
| `tools/verify_elf.py` | bionic linker 不变量（段/对齐/NEEDED/INIT_ARRAY） | 每次注入 |
| `tools/verify_apk.py` | APK 内容、DT_NEEDED、16 KiB 对齐 | 每次出包 |
| `tools/release.ps1` | 上述全部串成一条流水线，失败即中断 | 出包 |
| 编译器 `-Wall -Wextra` | 对 `native/` 全量开启，当前 0 warning | 每次构建 |

`release.ps1 -ChecksOnly` 只跑静态检查，便于改代码时快速回归。
