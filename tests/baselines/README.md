# 固定图回归资源

本目录只保留自动测试实际使用的基线，来自清理前的 revision `097d8c5`，文件内容保持不变。

- `images/`：63 张 PNG，供 `tools/*VisualRegression.cmake` 的固定机位回归使用。
- `reference-images/`：CPU volume-glass 的 Beauty 与 7 类 AOV，各含 HDR / PNG，供 `path-tracing-regression` 使用。

输出写入构建目录，默认不覆盖基线。回归阈值和对应场景由各验收脚本定义；只有显式启用 `UPDATE_BASELINES` 的脚本才会更新此目录。
