# FVP-Yuki ♡ 解封包工具

> 面向 **FVP引擎** 的图形化解包 / 封包 / 打补丁工具。

**[English →](README.md)**

---

> ⚠️ **免责声明与警告 (Disclaimer)**  
> 本工具的开发与发布**仅供技术交流与个人学习研究使用**，严禁用于任何商业用途或侵权行为。  
> 通过本工具所提取出来的游戏资源（图像、音频、文本等资料），其全部版权与知识产权均归原厂商所有。  
> **请低调使用，切勿大肆宣扬或将本工具用于大规模盗版、汉化商业牟利等行为**  
> 使用者因使用本工具而产生的一切后果与责任，由使用者自行承担。

---

## 1. 如何使用

只要两件东西：

1. Windows系统
2. 游戏为FVP引擎

至于「工具」本身的运行文件，只有这两个：

- `FVP-Yuki.exe`（主程序，双击运行）
- `FVP-Yuki-Core.dll`（核心库，必须和 exe 放在同一个目录里）

> ⚠️ **重要**：这两个文件 **必须放在一起**。少一个或分开放，程序启动会失败。

---

## 2. 放在哪里

推荐做法：把 `FVP-Yuki.exe` 和 `FVP-Yuki-Core.dll` **直接复制到游戏根目录**，也就是和 `xxx.hcb`、`graph.bin` 等数据文件同一层。这样工具会自动识别所有默认路径，不用自己填。

示意：

```
星辰恋曲的白色永恒 Finale\
├── AstralAirFinale.hcb
├── graph.bin
├── voice.bin
├── se_sys.bin
├── ...
├── FVP-Yuki.exe          ← 放这里
└── FVP-Yuki-Core.dll     ← 放这里
```

放好后双击 `FVP-Yuki.exe` 即可。

---

## 3. 界面长什么样

打开之后是一个粉色系的小窗口，上面有两个页签：

| 页签 | 作用 |
| --- | --- |
| **解包 Extract** | 把游戏的 `.hcb` / `.bin` 文件解开，看里面有什么。 |
| **封包 Pack** | 把你改好的文本或图片封回去，让游戏加载。 |

窗口最下方有：

- **状态条**：会告诉你现在在干嘛，成功还是失败。
- **进度条**：长任务会实时走动。
- **当前步骤**：更详细的一行说明。
- **打开结果** 按钮：任务完成后，直接跳到输出位置。

---

## 4. 解包

1. 打开 `FVP-Yuki.exe`。
2. 停留在 **解包 Extract** 页签。
3. 从资源管理器里，**把想看的文件拖进那块大粉色区域**：
   - 拖 `AstralAirFinale.hcb` → 会导出剧本文本到 `unpack\text\`
   - 拖 `graph.bin` → 会导出全部立绘 / CG / UI 到 `unpack\extracted_graph\`
   - 拖 `voice.bin` / `bgm.bin` / `se_sys.bin` → 音频导出到对应的 `unpack\extracted_xxx\`
4. 等进度条走完，状态条显示「解包完成」。
5. 点右下角 **打开结果**，就能直接看到导出的文件。

可以一次性拖多个文件进去，它会挨个处理。

---

## 5. 回写翻译文本

流程：

1. 先按上面的方法，把 `AstralAirFinale.hcb` 拖进来解一次。
2. 打开 `unpack\text\` 目录，会看到 `lines.jsonl` 和 `output.txt`。挑一个你喜欢的格式来翻译：
   - `lines.jsonl`：每行一条 JSON 记录，填写里面的 `translated_text` 字段。**推荐**，信息更完整。
   - `output.txt`：传统纯文本格式，把你的中文行放在以 `@` 开头的行里。
3. 回到工具，切到 **封包 Pack** 页签。
4. 默认的「源 HCB」「翻译文件」「输出 HCB」应该已经自动填好了。如果没填，点右上角 **刷新默认路径**，或者手动点 **浏览** 选一下。
5. 点右下角的 **封包文本 ♡** 按钮。
6. 几秒之后，工作目录下会生成 `output.hcb`。
7. 把 `output.hcb` 放回游戏目录就行。游戏会优先读它。

> 💡 如果翻译文件是 `.jsonl`，「输入格式」保持 `auto` 就行，工具会自动识别。

---

## 6. 替换图片 / CG / 音频

1. 先拖对应的 `.bin` 解包，比如 `graph.bin`。
2. 打开 `unpack\extracted_graph\preview\`，里面是 **PNG 预览图**，找到想改的那张。
3. 直接用你喜欢的画图软件改掉这张 PNG，**文件名保持不变**，**尺寸保持一致**。
4. 回到 **封包 Pack** 页签的「单个 BIN 重封」卡片：
   - 「资源目录」下拉框里挑 `unpack\extracted_graph`（或你要处理的那个）。找不到就点 **刷新**。
   - 「输出 BIN」默认已经填好。
   - **自动回写 PNG** 保持勾选 —— 这样修改过的 PNG 会被编码回游戏用的格式。
5. 点 **重封 BIN ✿**。
6. 生成的新 `.bin` 替换原始文件即可。

音频同理，替换 `raw/` 目录里的 `.hzc1` / `.ogg` 文件即可（保持原文件名）。

---

## 7. 一键构建补丁（给伙伴发包）

如果你同时改了文本 **和** 图片/音频，不想分别封包，用这个：

1. 保证 `unpack\text\` 和各个 `unpack\extracted_xxx\` 里都是你最新的修改。
2. 切到 **封包 Pack** 页签，找到底部的「一键构建补丁」卡片。
3. 「输出目录」保持默认（`patch_build\`）或自己指定一个。
4. 点 **一键构建 ♡**。
5. 稍等片刻，输出目录里会出现：
   - 新的 `output.hcb`
   - 改动过的所有 `.bin`
   - 一份 `patch_build_report.json`（告诉你改了哪些东西）
6. 把这些文件打包发给别人，对方丢到游戏目录里覆盖就是完整的汉化/修改补丁。

勾选 **包含未改动项** 可以把没改过的 `.bin` 也一起输出（体积大，适合完整重封）。

---

## 8. 常见问题

**Q：双击 exe 直接闪退？**
A：十有八九是 `FVP-Yuki-Core.dll` 没放在同一目录。检查一下。

**Q：拖文件进去没反应？**
A：工具只认 `.hcb` 和 `.bin`。其它类型（比如 `.anz`）请用 GARbro。

**Q：游戏启动崩溃？**
A：`output.hcb` 里翻译行可能包含游戏不支持的字符，或者文本过长。看看 **当前步骤** 里的错误提示，也可以直接把原版 `AstralAirFinale.hcb` 放回去排查。

**Q：改了 PNG 但游戏没变化？**
A：封包时要么「自动回写 PNG」没勾上，要么 PNG 尺寸和原图不同。重新检查后再封一次。

**Q：想重新开始？**
A：随时点右下角的 **重置状态** 按钮即可。

---

## 9. 目录速查

| 路径 | 存什么 |
| --- | --- |
| `unpack\text\lines.jsonl` | 结构化翻译文件（推荐编辑） |
| `unpack\text\output.txt` | 传统翻译文件 |
| `unpack\extracted_graph\preview\` | 图片 PNG 预览（用于编辑） |
| `unpack\extracted_graph\raw\` | 图片原始 hzc1 数据 |
| `unpack\extracted_voice\raw\` | 语音原始数据 |
| `output.hcb` | 文本封包输出，放回游戏目录即可生效 |
| `patch_build\` | 一键构建补丁的输出 |

---

## 10. 开发者 · 从源码构建

只想自己编译一份来玩/改的话，流程很简单：

- 装好 **Visual Studio 2022**（勾选「使用 C++ 的桌面开发」+「C++/CLI 支持」）。
- 打开 `FVP-Yuki.sln`，选 `Release | x64`，直接「生成解决方案」。
- 或者命令行：

    ```powershell
    msbuild FVP-Yuki.sln /p:Configuration=Release /p:Platform=x64
    ```

产物位于 `build\Release\`：

- `FVP-Yuki.exe` —— GUI 主程序（C++/CLI，依赖 .NET Framework 4.x，Win10/11 自带）
- `FVP-Yuki-Core.dll` —— 核心库（原生 C++，导出 `PackCpp*` 系列 API）

### 工程结构

```
FVP-Yuki/
├── FVP-Yuki.sln                解决方案
├── FVP-Yuki.vcxproj            GUI EXE 工程（C++/CLI）
├── FVP-Yuki-Core.vcxproj       核心 DLL 工程（原生 C++）
├── src/                        DLL 源码
│   ├── common.{h,cpp}          公用工具：编码、哈希、JSON、路径
│   ├── text_codec.{h,cpp}      Shift_JIS / GBK 自动判别
│   ├── hcb.{h,cpp}             HCB 脚本文本抽取与回写
│   ├── hzc1.{h,cpp}            HZC1 图片解码 / PNG 互转
│   ├── archive.{h,cpp}         BIN 容器解包、重封与补丁构建
│   ├── core_exports.{h,cpp}    对外 P/Invoke 接口
├── ui/                         GUI 源码
│   ├── main.cpp                wWinMain 入口
│   ├── MainForm.h              WinForms 主窗体
│   └── app.rc                  图标与背景资源
├── static/                     图标与窗口背景图
└── README.md
```

### 依赖

- Windows SDK（`<windows.h>`、Bcrypt）
- .NET Framework 4.x（C++/CLI、WinForms、`System.Drawing`、`System.Web.Extensions`）
- 无第三方包，无需 vcpkg

---

Enjoy your patching! ✿  若遇到 bug 欢迎反馈~
