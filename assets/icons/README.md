# Iris 应用图标

`iris-source.png` 是审核选定的第二版鸢尾花原图，保留生成时的完整分辨率与透明背景。图标采用浅紫与中紫花瓣，沿左下到右上约 45° 生长，呼应 Dandelion 的植物命名与 Iris 的光学意象。

`iris-icon.png` 是 256×256 的 GLFW 窗口图标；`iris.ico` 包含 16、20、24、32、40、48、64、128、256 像素的 Windows 图标资源，用于可执行文件、标题栏、任务栏与 Alt+Tab。原图由内置 image_gen 生成，按用户反馈修订并选定；生成时使用 `ip-as-logo` skill。

在项目根目录复现应用资源：

```powershell
./tools/GenerateAppIcon.ps1 -Source assets/icons/iris-source.png
```

脚本缩放原图，并移除与主体断开的像素；不会改写原图。`editor-atlas.png` 与 `lucide/` 是编辑器控件图标，来源与许可见该目录中的记录。
