<p align="center"><img src="public/res/app-icon.png" width="96" height="96" alt="ATS icon"></p>

# ATS — 自托管网站 / APP 流量统计

一个轻量的自托管流量统计工具：单二进制、SQLite、无外部运行时依赖。包含
埋点脚本、PV / 访客统计、来源 / 页面 / 国家排行、14 天趋势和管理后台。
管理界面、登录页、PV 徽章和 CSV 表头均为简体中文。

示例部署：<https://ats.ckern.cn>（域名、证书和 nginx 配置可按自己的环境替换）。

## 特性

- **单二进制**：C + [Kore](https://kore.io)，运行时只需要 SQLite 和可选的
  MaxMind GeoIP 数据库。
- **轻量**：默认 2 个 worker，常驻内存约 10 MB；静态资源由 nginx 直接提供。
- **埋点简单**：一个 `<script async src=".../rv.js/<app-id>">`。
- **看板**：访客数、PV、今日 / 昨日 / 7 天 / 30 天、14 天趋势、热门页面、
  来源、国家/地区、最近访问明细、CSV 导出（中文表头，带 UTF-8 BOM）。
- **多应用**：一个后台管理多个站点，每个应用支持最多 3 个域名。
- **隐私选项**：可隐藏 IP 末段、尊重 DNT、使用带密钥的访客标识。
- **安全**：参数化 SQL、HTML/JS 转义、JWT 会话、CSRF Origin 校验、
  埋点回填 HMAC 签名、登录限流、systemd 沙箱、loopback 后端。
- **可运维**：在线备份、保留期清理、GeoIP 热更新、健康检查、冒烟测试。

## 目录

```
ats-c/                  C 服务（Kore app，目录名必须是 ats-c）
  src/                  源码
  conf/                 Kore 配置和构建配置
  vendor/kore-4.2.3/    vendored Kore
  Makefile
db/schema.sql           SQLite schema（构建时嵌入二进制）
deploy/                 systemd / nginx / cron / 安装脚本
public/                 nginx 静态资源（/res、favicon、robots.txt）
tools/                  冒烟测试、HTTP 健壮性测试、维护、GeoIP、改密码
data/                   可选的 GeoIP 数据库（不随仓库分发）
.github/                CI / Dependabot
LICENSE                 MIT
THIRD_PARTY_NOTICES.md  第三方组件与数据许可
SECURITY.md             安全报告流程
```

## 构建依赖

- gcc、make、pkg-config
- sqlite3 开发包（`libsqlite3-dev` / `sqlite-devel`）
- libmaxminddb 开发包（`libmaxminddb-dev` / `libmaxminddb-devel`）
- PCRE2 开发包（`libpcre2-dev` / `pcre2-devel`）
- OpenSSL 开发包（仅 kodev 构建工具需要）
- `sqlite3` 命令行（部署后的备份 / 清理脚本使用）
- 可选：`shellcheck`、`cppcheck`（CI 使用）

Ubuntu/Debian:

```sh
sudo apt install build-essential libsqlite3-dev libmaxminddb-dev \
    libpcre2-dev libssl-dev sqlite3
```

Rocky/Alma/RHEL:

```sh
sudo dnf install gcc make sqlite-devel libmaxminddb-devel \
    pcre2-devel openssl-devel sqlite
```

## 构建

```sh
cd ats-c
make          # 默认 prod，输出 bin/ats-capi
make clean
```

Kore 的 kodev 会用当前目录名作为应用名，所以 **源码目录必须叫 `ats-c`**。

## 本地运行

```sh
cd ats-c
export ATS_ADMIN_USER=admin
export ATS_ADMIN_PASSWORD='local-dev-only'
export ATS_BASE_URL=http://127.0.0.1:3001
export ATS_COOKIE_DOMAIN=127.0.0.1
export ATS_JWT_KEY=local-dev-key-not-for-production
./bin/ats-capi -fnr
```

浏览器打开 <http://127.0.0.1:3001/>。首次启动会自动创建
`data/ats.db`、`data/body/`，并在没有 `ATS_JWT_KEY` 时生成 `data/jwt.key`。

GeoIP 数据库是可选的。没有它时服务正常运行，只是国家统计为空。获取方式见
“GeoIP” 一节。

冒烟测试（另开终端）：

```sh
ATS_ADMIN_PASSWORD=local-dev-only tools/smoke.sh http://127.0.0.1:3001
tools/http-fuzz-smoke.sh http://127.0.0.1:3001
```

## 配置

所有配置来自环境变量；生产环境放在 `/etc/ats-capi.env`（mode 0600）。

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `ATS_BASE_URL` | `http://127.0.0.1:3001` | 生成埋点、Cookie 跳转使用的地址；生产必须设置为公网 HTTPS 地址 |
| `ATS_COOKIE_DOMAIN` | `127.0.0.1` | 会话 Cookie 的 Domain；生产设置为站点域名 |
| `ATS_ADMIN_USER` | `admin` | 管理员用户名 |
| `ATS_ADMIN_PASSWORD` | 无 | 管理员明文密码（启动时计算 SHA-256） |
| `ATS_ADMIN_PASSWORD_SHA256` | 无 | 64 位十六进制摘要；推荐由安装脚本 / 改密脚本写入 |
| `ATS_JWT_KEY` | 自动生成 | HS256 签名密钥，同时用于埋点回填签名和访客 HMAC；未设置时使用 `data/jwt.key` |
| `ATS_TZ_OFFSET_MIN` | `480` | 展示和按天分桶的本地时区偏移（分钟） |
| `ATS_SESSION_SECONDS` | `43200` | 会话有效期（秒） |
| `ATS_RETENTION_DAYS` | `365` | 维护脚本清理多少天前的明细 |
| `ATS_ANONYMIZE_IP` | `0` | 设为 `1` 时 IPv4 抹掉最后一段、IPv6 抹掉接口标识后再入库 |
| `ATS_RESPECT_DNT` | `1` | 设为 `1` 时忽略带 `DNT: 1` 的浏览器；设为 `0` 可关闭 |
| `ATS_DB_PATH` | `<root>/data/ats.db` | SQLite 路径 |
| `ATS_GEOIP_DB_PATH` | `<root>/data/GeoLite2-Country.mmdb` | GeoIP 库路径 |

未设置 `ATS_ADMIN_PASSWORD` / `ATS_ADMIN_PASSWORD_SHA256` 时登录会关闭并打印警告。

## 部署

目标：Linux + systemd + nginx。

1. 准备证书（示例使用 `*.ckern.cn`）：

   ```sh
   /root/.acme.sh/acme.sh --issue --dns dns_ali \
       -d ckern.cn -d '*.ckern.cn' --keylength ec-256
   mkdir -p /etc/nginx/ssl/ckern.cn
   /root/.acme.sh/acme.sh --install-cert -d ckern.cn --ecc \
       --key-file /etc/nginx/ssl/ckern.cn/key.pem \
       --fullchain-file /etc/nginx/ssl/ckern.cn/cert.pem \
       --reloadcmd 'systemctl reload nginx'
   ```

2. 在仓库根目录执行：

   ```sh
   sudo deploy/install.sh --reset-data
   ```

   脚本会：构建、创建 `ats` 用户、安装到 `/webapps/ats-c`、生成
   `/etc/ats-capi.env`（打印一次管理员密码）、安装 systemd 服务、nginx
   配置、cron 维护任务，并把源码放到 `/opt/ats-src`。

3. 如需单独安装 nginx 配置：

   ```sh
   sudo install -m 0644 deploy/nginx-ats.conf \
       /etc/nginx/apps/ats.ckern.cn.conf
   sudo ln -sfn /etc/nginx/apps/ats.ckern.cn.conf \
       /etc/nginx/apps.enabled/ats.ckern.cn.conf
   sudo nginx -t && sudo systemctl reload nginx
   ```

4. 可选：限制后台只允许自己的 IP / 网段访问。参考
   `deploy/nginx-admin-allow.conf.example`，把管理路由放在一个带
   `allow` / `deny` 的 location 中；埋点路由 `/rv.js`、`/wru`、`/pv`
   必须保持公开。

## GeoIP

仓库不包含 MaxMind GeoLite2 数据库。使用自己的 MaxMind 账号更新：

```sh
# /etc/GeoIP-ats.conf 只包含 Country edition
geoipupdate -f /etc/GeoIP-ats.conf -d /webapps/ats-c/data
```

或者把你有权使用的 `GeoLite2-Country.mmdb` 放到
`ATS_GEOIP_DB_PATH`。服务会在文件变化后一分钟内自动重新加载。
许可与署名要求见 `THIRD_PARTY_NOTICES.md`。

## 使用

登录后创建应用，填写名称和最多 3 个域名（可以贴完整 URL，会自动归一化为域名）。
创建成功页和看板底部都会给出埋点代码：

```html
<script async src="https://ats.ckern.cn/rv.js/123456789"></script>
```

可选的国家 / PV 徽章：

```html
<div id="ats-pv"></div>
<script async src="https://ats.ckern.cn/pv/123456789/3/ats-pv"></script>
```

把域名替换成你自己的 `ATS_BASE_URL`。页面追踪依赖浏览器发送 `Referer`；
如果站点设置了 `Referrer-Policy: no-referrer`，只能统计 PV，无法得到页面来源
和回填的页面 URL。

## 隐私

默认会保存：访问时间、页面 URL、来源 URL、来源域名、IP、User-Agent、
国家、设备类型、浏览器、操作系统，以及一个访客标识。

- 访客标识是 `HMAC-SHA256(UA + 存储的 IP)`，不是明文 IP。
- `ATS_ANONYMIZE_IP=1` 会在入库前隐藏 IP 末段（IPv4 /24、IPv6 /48）。
- `ATS_RESPECT_DNT=1`（默认）会直接忽略带 `DNT: 1` 的访问。
- 明细默认保留 365 天，可用 `ATS_RETENTION_DAYS` 调整；维护脚本每天清理。
- 不使用 Cookie；但 IP 在多数法域仍属于个人信息。部署者需要自行提供隐私
  说明、确定合法性基础，并处理删除请求（可直接删除对应 `apps_traffic` 行）。

## 安全说明

- 后端只监听 `127.0.0.1:3001`，外网只能经 nginx 访问。
- 会话使用 HS256 JWT，含 `exp`，Cookie 为 `HttpOnly; SameSite=Lax`，
  HTTPS 下自动加 `Secure`；旧 token 在过期前仍然有效，登出只清除 Cookie。
- 所有 SQL 使用参数绑定；看板、登录页、CSV 导出对动态内容做转义。
- 登录限制为每 IP 20 次 / 5 分钟，全局 600 次 / 分钟。
- `/wru/` 回填 URL 带 HMAC 签名，不能靠猜 Snowflake ID 篡改别人的记录。
- 创建 / 修改 / 删除 / 登录会校验 `Origin`（有该头时），并配合
  `SameSite=Lax` 降低 CSRF 风险。
- `/rv.js` 的 Referer 域名校验只能阻止普通误用，不能阻止非浏览器伪造流量；
  这是客户端统计的固有限制。
- 运维密钥只放在 `/etc/ats-capi.env`、`/etc/GeoIP-ats.conf` 和
  `data/jwt.key`，不要提交到仓库。
- 备份包含访客数据，权限为 `640 root`；安装备份 tar 包为 `600`。

安全报告流程见 `SECURITY.md`。

## 运维

| 任务 | 方式 |
| --- | --- |
| 服务状态 | `systemctl status ats-capi` |
| 实时日志 | `journalctl -u ats-capi -f` |
| 健康检查 | `curl http://127.0.0.1:3001/__health` |
| 备份 / 清理 | `/etc/cron.d/ats`，每天 03:17；备份在 `/webapps/ats-c/backup` |
| GeoIP 更新 | 每周三 04:23，`/etc/GeoIP-ats.conf` 只更新 Country |
| 手动维护 | `sudo /webapps/ats-c/tools/maintenance.sh` |
| 修改管理员密码 | `sudo /webapps/ats-c/tools/set-password.sh` |
| 回滚二进制 | 安装脚本会在 `/webapps/ats-c.bak-<时间>.tgz` 留备份 |

备份是 SQLite 在线 `.backup` 后 gzip，保留 14 天；恢复时解压并替换
`data/ats.db`，然后重启服务。

## 数据模型

- `apps`：应用名、最多 3 个域名、备注。
- `apps_traffic`：每次访问一行，时间统一存 UTC。
- 明细保留期由 `ATS_RETENTION_DAYS` 控制，默认 365 天。

## 开发与测试

```sh
tools/ci-local.sh prod   # 构建 + 启动 + smoke + HTTP 健壮性测试
tools/ci-local.sh asan   # ASan/UBSan 构建 + 同样的测试
```

CI（`.github/workflows/ci.yml`）会在 Ubuntu 上执行：

- prod 和 ASan/UBSan 两套构建与冒烟测试
- `shellcheck`
- `cppcheck`
- `gitleaks` 密钥扫描

`cppcheck` 目前是 advisory（不阻塞 CI），后续可以把告警清零后再设为强制。

## 许可证

本项目使用 MIT License，见 `LICENSE`。

第三方组件、移植代码和 GeoLite2 数据的许可/署名见
`THIRD_PARTY_NOTICES.md`。如果 fork 后替换了国旗图标、favicon 或
GeoLite2 数据库，请同步更新该文件。

## 从旧版本迁移

旧版 PHP/CodeIgniter 的数据不需要迁移。安装时使用 `--reset-data` 会清空
`ats.db` 并从空库开始；旧目录会先被打包备份。旧版源码和旧域名不再属于本项目。
