# SVG 无损瘦身

图标统一改用 SVG，工具跳过 PNG/JPEG 等位图格式的既有行为。SVG 走 Leanify 的
XML 通道，新增三项 DOM 级瘦身，并由渲染器做逐像素门禁：

1. **去注释** —— pugixml 默认不加载注释节点，重写输出时注释自然消失。
2. **合并重复定义** —— `<defs>` 中除 `id` 外结构完全相同的
   `linearGradient`/`radialGradient`/`pattern`/`filter`/`marker`/`mask`/
   `symbol`/`clipPath` 只保留一份，`href="#x"`、`xlink:href="#x"`、
   `fill="url(#x)"`、`style` 内 `url(#x)` 等引用统一改写。
3. **精简数值精度** —— 仅对白名单几何/路径属性（`x/y/cx/cy/r/d/points/
   transform/...`）中的无单位定点数字按可配置上限 `--svg-precision 0..15`
   四舍五入，颜色、标识符、`style` 文本一律不碰；科学计数法（`1.5e-2`）、
   超过 double 精确整数范围的大数、负零归一化都有专门处理。只有真正变短的
   token 才替换，单文件层面不可能因此变大。

## 组件

- `formats/svg.h`、`formats/svg.cpp`：瘦身实现（合并、引用改写、精度、跳过记录）。
- `formats/xml.cpp`：SVG 分支调用入口；新增嵌套深度安全预检（默认 10000 层）。
- `main.cpp`：新增 `--svg-precision` 选项，缺省 `-1` 表示关闭精度精简（legacy）。
- `svg-slim/svg-slim.sh`：渲染门禁驱动，逐档试精度并只接受像素一致且更小的结果。
- `svg-slim/test/`：语料、渲染器封装（resvg）、像素对比（Pillow）、端到端脚本。

## 不支持特性的跳过策略

子树出现 `foreignObject`、`script`、`style`、`animate`、`animateMotion`、
`animateTransform`、`set` 时，该子树整体不参与合并与精度精简（注释、元数据等
通用清理仍按原逻辑作用于其它部分），并在 `-v` 输出中逐条记录，例如：

```
SVG: skipping unsupported feature <animate>, subtree kept unchanged.
SVG: merging duplicate <linearGradient> #gradB -> #gradA
```

解析失败或嵌套超过 10000 层的文件判定为无效/不安全，整体透传，字节不变。

## “原格式零变化”的含义

输出仍是单根 `<svg>` 的 XML 文档，命名空间、元素、属性语义保持不变；变化仅限于
注释/默认属性/元数据移除、重复定义合并、数值缩短、空白压缩，均为 XML 规范化允许
的等价改写。文件不得变大由两级保证：C++ 端仅接受更短 token，最终写盘前比较
`writer.buf.size() < size_`，变大则 memmove 原内容；驱动端再做一次整体大小断言。

## 渲染门禁为什么必须存在

四舍五入到任意固定位数都无法从数学上保证亚像素栅格化结果不变，而且误差是非单调的
（本仓库语料中精度 3、5、6 都出现过像素差，4 和 7 反而为 0）。因此
`svg-slim.sh` 从请求的精度上限开始逐档尝试，每档都用固定版本 resvg 渲染并逐像素
对比，只接受 `differing_pixels == 0` 且体积更小的候选；全部失败时回退到不含精度
精简的结果。裸用 `leanify --svg-precision N` 属于“无门禁”模式，其像素误差会被
测试报告单独记录（`ungated3_demo` 行），不作为交付路径。
