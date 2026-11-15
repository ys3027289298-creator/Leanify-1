# 复现步骤

## 一次性环境准备

```bash
# 1. 编译 Leanify（需要 g++/clang++ 与 make）
make

# 2. 渲染器（固定版本，避免渲染器差异污染像素对比）
mkdir -p /tmp/svgtool && cd /tmp/svgtool
npm init -y
npm install @resvg/resvg-js@2.6.2

# 3. 像素对比用的 Python 环境（系统 Python 受 PEP 668 保护时用 venv）
python3 -m venv /tmp/venv
/tmp/venv/bin/pip install pillow
```

## 跑完整验证

```bash
cd <repo>
./svg-slim/test/run_tests.sh
# 退出码 0 = 全部通过；报告：
#   svg-slim/test/out/report.json        机器可读
#   svg-slim/test/out/report.pretty.json 格式化
#   svg-slim/test/out/verbose.log         合并/跳过记录
#   svg-slim/test/out/<name>/*.png        前后渲染图
```

## 单独处理一个 SVG

```bash
# 仅 DOM 瘦身（注释/合并/默认属性），无精度精简
./leanify -v icon.svg

# 渲染门禁 + 精度上限 3（推荐路径，保证像素一致且不增大）
./svg-slim/svg-slim.sh --svg-precision 3 --verbose icon.svg icon.min.svg

# 裸跑固定精度（无门禁，仅用于观测误差，不作为交付路径）
./leanify --svg-precision 3 icon.svg
```

## 内存与深度实验

```bash
# 峰值 RSS / 耗时
/usr/bin/time -v ./leanify --svg-precision 4 big.svg

# 嵌套深度边界（9000 正常，>=10001 透传并记录 skipping）
python3 -c "n=11000; open('/tmp/deep.svg','w').write(
  '<svg xmlns=\"http://www.w3.org/2000/svg\">'+'<g>'*n+'<rect width=\"1\" height=\"1\"/>'+'</g>'*n+'</svg>')"
./leanify -v /tmp/deep.svg

# 内存/泄漏检测
make asan
ASAN_OPTIONS=detect_leaks=1 ./leanify -v --svg-precision 3 svg-slim/test/inputs/*.svg
```
