# 推送到 GitHub / Pushing to GitHub

本目录已是一个可直接推送的仓库内容（README、固件、源码、烧录脚本）。
选一种方式把它发到 `ESP32 Attitude Indicator` 仓库。

---

## 方式 A：我帮你推（推荐，最快）

需要两样东西：

1. 你的 **GitHub 用户名**
2. 一个 **Personal Access Token**（细粒度 token，权限只要 `Contents: Read and write`，
   仓库范围选"仅新建的这一个"或 "All repositories"）

拿到后我会用 GitHub API 建仓库并推送，全程不需要你安装任何东西。

> 安全提示：token 用完就可以在 GitHub 设置里 **立刻删除**（Settings → Developer settings →
> Personal access tokens）。建议创建一个**只对目标仓库有 contents 写权限、有效期 7 天**的 token。

---

## 方式 B：你本机装 git + gh（以后维护都方便）

```powershell
winget install --id Git.Git -e
winget install --id GitHub.cli -e
# 重开一个终端后：
gh auth login                     # 选 GitHub.com → HTTPS → 浏览器登录
cd <本目录>
git init
git add .
git commit -m "ESP32 Attitude Indicator v1.0.0"
git branch -M main
gh repo create "ESP32-AttitudeIndicator" --public --source . --remote origin --push
```

以后更新：

```powershell
git add .
git commit -m "更新说明"
git push
```

---

## 方式 C：纯网页上传（不装任何工具）

1. 打开 <https://github.com/new>
2. Repository name 填 **`ESP32 AttitudeIndicator`**（GitHub 会自动把空格变成 `-`），选 Public
3. 创建后点 **uploading an existing file**
4. 把本目录**所有内容**拖进去（注意要保持 `firmware/`、`src/`、`docs/` 的目录结构：
   网页上传支持拖整个文件夹）
5. Commit

> 单文件上限 25 MB，本包最大文件 1.3 MB，没问题。

---

## 发布后建议（可选）

- 打一个 tag：`git tag v1.0.0 && git push --tags`
- 在 GitHub **Releases** 里把 `firmware/attitude-indicator-merged.bin` 作为附件上传，
  这样别人不需要克隆整个仓库也能下载固件
- 仓库 Description 填：`Flight-instrument style artificial horizon for Waveshare ESP32-S3-Touch-LCD-1.85`
- Topics：`esp32`, `esp32-s3`, `lvgl`, `attitude-indicator`, `artificial-horizon`, `arduino`
