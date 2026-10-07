# 固定图回归资源

本目录只保留自动测试实际使用的基线。初始迁移来自清理前的 revision `097d8c5`；后续明确授权的更新如下，不改写历史阶段说明图。

2026-10-06：用户审查并接受独立光空间取样修复，更新 Glass3 的 `lightspace_msaa1`、`lightspace_msaa4`、`caustics_off`、`projector`、`caustics_debug` 与 Glass4 的 `caustics_final`、`caustics_dispersion_off`、`caustics_off`、`caustics_msaa1`，共 9 张 PNG。文件均使用 `glass3_` / `glass4_` 前缀；场景、相机及原 MAE 0.015 / changed fraction 0.08 门槛保持不变。其他基线未更新，当前结果见本地 M1-B 验收记录。

- `images/`：63 张 PNG，供 `tools/*VisualRegression.cmake` 的固定机位回归使用。
- `reference-images/`：CPU volume-glass 的 Beauty 与 7 类 AOV，各含 HDR / PNG，供 `path-tracing-regression` 使用。

输出写入构建目录，默认不覆盖基线。回归阈值和对应场景由各验收脚本定义；基线维护需明确授权，可以通过 `UPDATE_BASELINES` 重拍，或核对 SHA256 后仅复制已审查的指定候选。
