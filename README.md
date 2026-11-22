<p align="center"><img src="public/res/app-icon.png" width="96" height="96" alt="ATS icon"></p>

# ATS — 自托管网站 / APP 流量统计

<p>
  <a href="https://github.com/ckern-cn/ats-analytics/actions/workflows/ci.yml"><img src="https://github.com/ckern-cn/ats-analytics/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="MIT">
  <img src="https://img.shields.io/badge/UI-zh--CN%20%7C%20en-0f766e" alt="UI languages">
</p>

一个轻量的自托管流量统计工具：单个应用二进制、SQLite；不依赖语言运行时
（动态链接系统 SQLite、libmaxminddb、PCRE2）。包含埋点脚本、
PV / 访客统计、来源 / 页面 / 国家排行、14 天趋势和管理后台。

默认界面为简体中文，支持英文；语言包可扩展。示例部署：
<https://ats.ckern.cn>（域名和部署环境可按自己的需要替换）。

## 特性

- **单二进制**：C + [Kore](https://kore.io)，运行时需要系统 SQLite、libmaxminddb、PCRE2；
  MaxMind GeoIP 数据库可选。
- **轻量**：默认 2 个 worker，常驻内存约 10 MB；静态资源由 nginx 提供。
- **埋点简单**：一个 `<script async src=".../rv.js/<app-id>">`。
- **看板**：访客数、PV、今日 / 昨日 / 7 天 / 30 天、14 天趋势、热门页面、
  来源、国家/地区、最近访问、CSV 导出。
- **多应用**：每个应用支持最多 3 个域名，支持编辑和删除。
- **多语言**：`zh-CN` / `en`，支持 `Accept-Language`、Cookie 和
  `/lang/<code>`。
- **隐私选项**：可隐藏 IP 末段、尊重 DNT、使用带密钥的访客标识。
- **安全**：参数化 SQL、HTML/JS 转义、JWT、CSRF Origin 校验、`/wru` HMAC、
  登录限流、systemd 沙箱。
- **可运维**：在线备份、保留期清理、GeoIP 热更新、健康检查、CI 和冒烟测试。

## 快速开始

依赖和完整步骤见 [安装文档](docs/installation.md)。

```sh
cd ats-c
make

export ATS_ADMIN_USER=admin
export ATS_ADMIN_PASSWORD='local-dev-only'
export ATS_BASE_URL=http://127.0.0.1:3001
export ATS_COOKIE_DOMAIN=127.0.0.1
export ATS_JWT_KEY=local-dev-key-not-for-production

./bin/ats-capi -fnr
```

打开 <http://127.0.0.1:3001/>。首次启动会创建 `data/ats.db`、`data/body/`,
并在未设置 `ATS_JWT_KEY` 时生成 `data/jwt.key`。

测试：

```sh
ATS_ADMIN_PASSWORD=local-dev-only tools/smoke.sh http://127.0.0.1:3001
tools/http-fuzz-smoke.sh http://127.0.0.1:3001
```

一键构建 + 启动 + 测试：

```sh
tools/ci-local.sh prod
tools/ci-local.sh asan
```

## 部署

目标：Linux + systemd + nginx。

```sh
sudo deploy/install.sh --reset-data
```

脚本会安装到 `/webapps/ats-c`、源码到 `/opt/ats-src`、生成
`/etc/ats-capi.env`、安装 systemd/nginx/cron，并打印一次管理员密码。
证书、GeoIP、nginx 和升级步骤见：

- [安装与部署](docs/installation.md)
- [配置项](docs/configuration.md)
- [运维手册](docs/operations.md)

## 多语言

- 默认 `zh-CN`，支持 `en`。
- 选择顺序：`?lang=` > `ats_lang` Cookie > `Accept-Language` >
  `ATS_DEFAULT_LOCALE`。
- 登录页和后台顶栏有语言切换；CSV 和 PV 徽章跟随请求语言。
- 新增语言步骤见 [i18n 文档](docs/i18n.md)。

## 隐私与安全

- 默认保存访问时间、页面/来源 URL、IP、UA、国家、设备/浏览器/系统和访客标识。
- 访客标识是带密钥的 HMAC，不是明文 IP；可开启 IP 匿名化、尊重 DNT。
- 默认保留 365 天，可通过 `ATS_RETENTION_DAYS` 调整。
- 部署者需要自行提供隐私说明并遵守当地法律（GDPR / PIPL 等）。
- 安全问题和数据说明见 [隐私文档](docs/privacy.md) 与
  [安全策略](SECURITY.md)。

## 项目原则

| 目标 | 做法 |
|---|---|
| 功能免费 | 所有功能都在 MIT 代码中，无付费墙、License key 或时间限制 |
| 捐赠自愿 | 捐赠不买功能、优先支持或路线图影响力；见 [DONATE.md](DONATE.md) |
| 贡献低门槛 | 无 CLA，DCO sign-off；代码、文档、翻译、测试都欢迎；见 [CONTRIBUTING.md](CONTRIBUTING.md) |
| 品牌/发布可控 | 代码 MIT；ATS 名称、Logo、域名和官方发布由维护者控制；fork 需改名换标；见 [TRADEMARK.md](TRADEMARK.md) |

维护者最终决定路线图、合并、品牌使用和官方发布：见
[GOVERNANCE.md](GOVERNANCE.md)。

## 文档索引

- [安装与部署](docs/installation.md)
- [配置](docs/configuration.md)
- [运维](docs/operations.md)
- [架构](docs/architecture.md)
- [多语言](docs/i18n.md)
- [隐私](docs/privacy.md)
- [开发与测试](docs/development.md)
- [FAQ](docs/faq.md)

## 参与

- 报告 bug / 提需求：使用 issue 模板，先搜索已有 issue 和文档。
- 提交代码：`git commit -s`，保持 PR 小而聚焦。
- 翻译：复制 `ats-c/src/lang/en.h`，按 [i18n 文档](docs/i18n.md) 添加语言。
- 安全问题：不要公开提 issue，见 [SECURITY.md](SECURITY.md)。

## 工作室

ATS 由 [ckern studio](https://ckern.cn/) 维护。ckern 是一家只做本地优先软件的独立工作室。

## 许可与品牌

- 源代码：[MIT License](LICENSE)。
- 第三方组件与数据：[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
- 名称、Logo、图标、域名和官方发布：[TRADEMARK.md](TRADEMARK.md) 与
  [NOTICE](NOTICE)。
- 官方仓库：
  - GitHub <https://github.com/ckern-cn/ats-analytics>
  - Gitee <https://gitee.com/mywliu/ats-analytics>

只有上述官方渠道发布的构建才是官方版本。Fork 可以自由发布，但必须使用不同的
项目名称和视觉标识，不能暗示官方身份。
