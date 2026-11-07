# CloseCrab 营销策略文档

## 核心定位

**一句话卖点：** 3.2 MB，59 个工具，跑在你自己 GPU 上的 AI 编程助手 — 买断制，$29.99，代码永不离机。

**差异化三角：**
- 极致小（3.2 MB 单二进制，零依赖）
- 极致私（本地推理，代码不上云）
- 极致便宜（$29.99 买断，不订阅）

---

## 一、视频演示策略

### 必做的核心 Demo 视频（按优先级排）

#### 视频 1 — 60 秒震撼开场（最高优先级）
**标题：** "3.2 MB 的 AI 编程助手，比 Cursor 便宜 20 倍"
**脚本节奏：**
1. 0-5s：黑屏终端，`ls -lh closecrab.exe` → 显示 3.2 MB
2. 5-15s：直接 `./closecrab` 启动，冷启动 2.4s，展示工具列表
3. 15-40s：真实任务 — 让它重构一段函数，展示多 agent 协作（Coder/Tester/Reviewer 并行）
4. 40-55s：`nvidia-smi` 确认推理跑在本机 GPU，网络监控显示零出站流量
5. 55-60s：结尾卡片 — "$29.99 · 买断 · blitzball.lol"

**平台：** X/Twitter 首发，剪辑竖版发 TikTok / 抖音 / B站

#### 视频 2 — "Team Mode 实战"（5 分钟）
展示 4 个 agent 并行处理一个大型 Python 项目：
- Researcher 索引代码库
- Coder 写实现
- Tester 跑测试
- Reviewer 给建议
强调：所有推理在本地，无 rate limit，无 API 费用叠加

#### 视频 3 — "Mobile Remote 酷炫演示"（2 分钟）
手机画面 + 电脑画面分屏：
- 离开桌子，掏出手机
- 手机上看到 agent 思考动画（thinking bar）
- 输入指令，电脑上自动写代码
- 展示 9 个启动小游戏（Snake/Tetris）
这个视频特别适合社交媒体，视觉冲击力强

#### 视频 4 — "vs Cursor/Copilot 对比"（3 分钟）
| 维度 | CloseCrab | Cursor | GitHub Copilot |
|---|---|---|---|
| 价格 | $29.99 买断 | $20/月 | $10/月 |
| 数据隐私 | 本地 | 上云 | 上云 | 
| 离线使用 | 完全支持 | 不支持 | 不支持 |
| 二进制大小 | 3.2 MB | 数百 MB Electron | 扩展 |

---

## 二、发布渠道与节奏

### 英文受众（主要付费来源）

**X/Twitter — 最高 ROI 平台**
- 每周发 2-3 条技术 thread，格式：问题 → 演示 → 下载链接
- 关键 hashtag：`#llm` `#localai` `#rust` `#cpp` `#privacy` `#buildinpublic`
- 目标账号互动：关注 @simonw、@karpathy、@thorstenball 等有影响力的开发者
- 发帖时间：UTC 14:00-17:00（覆盖美西上班前 + 欧洲下班后）

**Hacker News — 一次爆发可带来数百销售**
- 投稿标题模板：
  - "Show HN: CloseCrab – 3.2 MB C++ AI coding agent, 59 tools, runs on your GPU"
  - "Ask HN: We built an AI coding assistant that fits in 3.2 MB – what features matter most?"
- 发帖时间：美东时间周一至周二早上 8-9 点
- 提前在评论区准备好技术细节回答（binary size why、MCP protocol、Team Mode 架构）

**Reddit**
- r/LocalLLaMA — 本地推理社区，CloseCrab 天然契合
- r/programming — 发技术对比帖
- r/selfhosted — 强调"代码不离机"的隐私卖点
- r/SideProject — buildinpublic 风格的故事帖

**YouTube**
- 发完整教程：从安装到第一个 Team Mode 任务，15-20 分钟
- 标题 SEO：针对"local AI coding assistant"、"offline AI agent"、"Claude Code alternative"

### 中文受众

**B站**
- 标题：「3.2MB的AI编程助手，比Cursor便宜20倍，代码完全本地」
- 投稿分区：科技 / 计算机技术
- 配合弹幕互动

**微信/即时渠道**
- 技术社群（v2ex、掘金、sspai）发图文测评
- 知乎：写「如何搭建完全本地的 AI 编程工作流」，软植入 CloseCrab

---

## 三、定价与转化策略

### 当前定价分析
- $29.99 买断 — 非常有竞争力，但需要主动说出来
- 对比锚点永远要出现在文案里：Cursor $20/月 = 1.5 个月就回本

### 降低购买摩擦的具体动作
1. **提供免费试用版** — 功能完整但有 token 限制或工具数限制，让用户先上手
2. **展示 license key 流程** — 录一个 30 秒视频：买 → 收邮件 → 输入 key → 开始用，消除顾虑
3. **30 天退款政策要在所有宣传里写明** — 这是购买保险，要放大写
4. **支持 Alipay** — 对中国用户是硬需求，已经支持了，要在中文渠道重点提

### 捆绑销售机会
- CloseCrab + Blitz DAW 套装定价 $149（单独 $29.99 + $150 = $180，省 $30）
- 对于已有 CloseCrab 的用户，Blitz 升级价 $129

---

## 四、内容营销 — 会产生长期搜索流量的文章

1. **"How I built a 3.2 MB AI coding agent in C++"** — 技术 deep dive，吸引 HN/Reddit
2. **"Why I stopped using Cursor"** — 对比帖，SEO 流量大
3. **"Running LLMs locally for code: a practical guide"** — 引流到 CloseCrab 的 local 推理能力
4. **"MCP protocol explained with real examples"** — 技术教程，建立专业性
5. **"Team Mode: how multiple AI agents collaborate on your codebase"** — 展示独特功能

---

## 五、社区建立

- 开 Discord 服务器：技术支持 + 用户分享 + 新功能预告
- GitHub Issues 要快速响应 — 每个 star 都是潜在买家
- 建立"power user"群：给早期买家特权（beta 功能、直接反馈渠道），他们会自发传播

---

## 六、关键指标

| 指标 | 目标（3个月） |
|---|---|
| 官网月 UV | 10,000+ |
| HN 首发帖子分数 | 200+ |
| GitHub Stars | 500+ |
| 月销售量 | 100+ |
| 转化率（访客→购买） | 2-3% |

---

## 七、最高优先级行动清单

1. 录视频 1（60 秒震撼开场）— 这一个视频值所有其他工作的总和
2. 写 HN Show HN 帖，选好发帖时机
3. 在 blitzball.lol 首页加上「vs Cursor/Copilot」对比表
4. 给 CloseCrab GitHub README 加徽章：binary size / tools count / no telemetry
5. 在定价页写大字：「Cursor 1.5 个月的钱，买断终身」
