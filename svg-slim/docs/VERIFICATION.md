# 验证报告（四项必交付物）

环境：Ubuntu 24.04，g++ 13、Node v18.19.1、`@resvg/resvg-js@2.6.2`（固定渲染器，
480px 宽、白底）、Pillow 13.0.0。所有数字可用 `./svg-slim/test/run_tests.sh` 复现，
机器可读结果在 `svg-slim/test/out/report.json`。

## 1. 改造前后对比数据（同一组输入）

三种模式：`legacy`（不开新精度开关，等同旧版输出路径）、`merge`（注释+合并+默认
属性清理）、`gated`（驱动渲染门禁 + 精度上限 3）。渲染对比的 PNG 尺寸均为
480x320 或 480x480。

| 输入 | 模式 | 前(bytes) | 后(bytes) | 节省 | 像素差异 | 最大通道误差 |
|---|---|---:|---:|---:|---:|---:|
| 01-gradient-symbol | legacy | 1913 | 1257 | 34.292% | 0 | 0 |
| 01-gradient-symbol | merge | 1913 | 1257 | 34.292% | 0 | 0 |
| 01-gradient-symbol | gated | 1913 | 1257 | 34.292% | 0 | 0 |
| 02-precision | legacy | 647 | 553 | 14.529% | 0 | 0 |
| 02-precision | merge | 647 | 553 | 14.529% | 0 | 0 |
| 02-precision | gated | 647 | 471 | 27.202% | 0 | 0 |
| 03-unsupported | gated | 1019 | 653 | 35.918% | 0 | 0 |
| 04-already-min | gated | 111 | 110 | 0.901% | 0 | 0 |
| 05-metadata-style | gated | 609 | 413 | 32.184% | 0 | 0 |
| 06-dedup-kinds | legacy | 1357 | 832 | 38.688% | 0 | 0 |
| 06-dedup-kinds | gated | 1357 | 815 | 39.941% | 0 | 0 |
| 07-inner-id | gated | 1125 | 674 | 40.089% | 0 | 0 |

没有任何一行 `grew=true`；全部 7 个 `gated` 行 `identical=true`。`07-inner-id`
专门验证：外层相同但内部 id 不同的两个 `symbol` 不会被误并（`outer1/outer2` 均保留），
只有结构完全相同的渐变被合并。

### 指定样例：含渐变与符号引用的 SVG

`svg-slim/test/inputs/01-gradient-symbol.svg`（线性渐变 ×2 重复定义、径向渐变、
`symbol` + 两处 `use`、注释、高精度坐标）：

- 体积：1913 → 1257 字节，减少 656 字节（34.29%）。
- 合并：重复的 `#gradB` 并入 `#gradA`，两处 `url(#gradB)` 引用自动改写；
  产物中 `gradB` 出现 0 次、`url(#gradA)` 出现 2 次，注释 0 处。
- 渲染对比（480x320，153600 像素）：差异像素 0，最大逐通道误差 0；
  `demo/before.png` 与 `demo/after.png` 连 PNG 字节都完全一致（`cmp` 通过）。
- 无门禁裸跑 `--svg-precision 3` 对该文件同样 0 像素差（其坐标多为 3 位小数，
  合并主导瘦身）；体积同为 1257。

## 2. 失败路径：可复现步骤与预期错误

| # | 步骤 | 预期 | 实测退出码 |
|--|---|---|---:|
| 1 | `./leanify --svg-precision 99 f.svg` | stderr 提示 `There should be a number between 0 and 15...` 并打印用法 | 1 |
| 2 | `./leanify --svg-precision -1 f.svg` | 同上（前导 `-` 被识别为缺值/非法值，不会越界读取） | 1 |
| 2b | `./leanify --svg-precision abc f.svg`、`./leanify --svg-precision`（缺值） | 同上错误信息与退出码 1，不会静默回退到精度 0 | 1 |
| 3 | `./svg-slim/svg-slim.sh /no/such/file.svg` | `error: input not found: ...` | 2 |
| 4 | `./svg-slim/svg-slim.sh --svg-precision abc f.svg` | `error: --svg-precision must be an integer between 0 and 15, got 'abc'` | 2 |
| 5 | 构造未闭合标签的 SVG 后 `./svg-slim/svg-slim.sh bad.svg out.svg` | resvg 报 `root node was opened but never closed`，驱动输出 `renderer rejected the input SVG; leaving file untouched`，不产生 out.svg | 3 |
| 6 | 非 SVG 文本文件走 `./leanify notsvg.txt` | `Format not supported!`，文件原样透传，`cmp` 一致 | 0 |
| 7 | 嵌套 11000/100000 层的 SVG | `XML/SVG nesting exceeds safety limit, skipping.`，判定不支持并透传，大小不变 | 0 |
| 8 | 无门禁精度触发亚像素差（`02-precision.svg`，`--svg-precision 3`） | 输出 451 字节（省 30.29%），15 个差异像素、最大通道误差 16；该结果只出现在 `ungated3_demo` 记录行，gated 流程自动拒绝并回退到 471 字节、0 差异的 p=4 候选 | 0 |

复现命令：

```bash
make
./leanify --svg-precision 99 svg-slim/test/inputs/02-precision.svg   ; echo $?
printf '<svg xmlns="http://www.w3.org/2000/svg"><unclosed>' > /tmp/bad.svg
./svg-slim/svg-slim.sh /tmp/bad.svg /tmp/out.svg                     ; echo $?
```

## 3. 资源 / 内存上界

### 时间与内存（release 构建，`/usr/bin/time -v` 峰值 RSS）

| 负载 | 输入大小 | 输出大小 | 峰值 RSS | RSS/输入 | 墙钟 |
|---|---:|---:|---:|---:|---:|
| 2 万圆 + 400 渐变（`big`） | 1,684,196 B | 1,371,138 B | 13,884 KB | ~8.4x | 0.04 s |
| 2000 个重复 50-stop 渐变（`dup`，合并最坏情况） | 4,835,864 B | 95,120 B | 28,704 KB | ~6.1x | 0.17 s |

### 静态上界说明

- 解析与输出缓冲：pugixml DOM 与一次序列化缓冲，均为 O(n)，n 为输入字节数；
  合并 pass 额外为每个可去重定义保存一份规范化字符串，总量不超过 defs 子树大小，
  仍为 O(n)。实测内存放大 ≤ 8.4 倍输入，工程上按 **峰值堆内存 < 10n + 32 MB**
  预留（32 MB 覆盖运行时与 zopfli/mozjpeg 等静态依赖的固定占用）。
- 嵌套深度：递归遍历每深度一栈帧，硬性安全上限 `kMaxNestingDepth = 10000`；
  超限直接透传。默认 8 MiB 栈下实测 9000 层正常处理、11000 层起被拦截，无栈溢出。
- 精度 pass 为单趟线性扫描，临时 token 缓冲 ≤ 单属性长度，不随文件数增长；
  门禁驱动每轮只保留 1 份原始 + 1 份候选 + 2 张 PNG（固定 480px 宽），磁盘临时
  空间上界约为 `2n + 2 × 480 × H × 4` 字节，退出时自动清理。
- ASAN+UBSan（`make asan`，`ASAN_OPTIONS=detect_leaks=1`）跑完全部语料及
  big/dup 负载：0 个 `ERROR/Leak/runtime error`。

## 4. 旧行为一致性（逐位 / 逐像素）

- **逐像素**：上表全部 gated 行在固定 resvg 渲染下差异像素为 0、最大通道误差 0；
  指定渐变样例的前后 PNG 经 `cmp` 逐字节相同。
- **逐位（legacy 模式）**：`04-already-min.svg` 在不开 `--svg-precision` 时，
  除 pugixml 不保留的结尾换行外，内容字节逐位相同（测试用去换行后 `diff` 断言）。
  这与改造前 XML/SVG 通道的既有行为一致：新代码默认 `svg_precision=-1`，精度
  pass 完全不执行；合并 pass 只在存在结构相同的重复定义时改变输出，无重复定义的
  文件输出与旧版相同。
- **不得变大**：C++ 端写盘前比较大小，变大则 memmove 原内容；18 个 gated/legacy/
  merge 结果行无一变大；`04-already-min` 这类无法再优化的文件保持 110/111 字节
  （仅少结尾换行）。
- **不支持特性保真**：`03-unsupported.svg` 中 `<animate>` 子树、
  `05-metadata-style.svg` 中 `<style>` 块在产物中逐字符保留（含原始高精度数值），
  合并与精度只作用于文档其余部分，渲染 0 差异。
