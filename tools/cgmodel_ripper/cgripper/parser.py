"""详情页解析：支持 /model/<id>.html (模型) 与 /art/<id>.html (作品)。

站点结构要点 (2026-08 实测):
  * 缩略图 <li class="detail-thumb-item" data-rel="//img.cgmodel.com/.../cover/xxx.webp">
    把路径中的 /cover/ 换成 /big/ 即得大图 (无 origin/source 级别)。
  * 视频通过 data-video="<md5>" 占位, 需 POST /works/index-out-init.html
    action=model-GetVideoAll 换取 //video.cgmodel.com/.../index.m3u8
  * 文字说明位于 <div class="model_explain"> / .cgmodel-work-dcl 内, 需剔除
    站点通用的版权声明模板段落。
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from bs4 import BeautifulSoup

IMG_HOST_RE = re.compile(r"img\.cgmodel\.com", re.I)

# 站点固定免责声明/协议片段, 出现即视为非作者描述
BOILERPLATE = (
    "版权说明：在本站下载的素材",
    "本内容为三维数字模型",
    "声明：水印不代表署名",
    "购买前请仔细查看模型说明",
    "如遇模型文件无法打开",
    "本站所有素材仅限",
    "举报",
)

STOP_LABELS = {
    "模型说明", "作品说明", "评论", "举报", "分享", "复制分享链接",
    "分享到微博", "分享到QQ", "分享到空间", "微信扫码分享", "点赞",
    "模型参数", "搜相似", "立即下载", "加入购物车", "售前咨询",
}

PARAM_KEYS = (
    "文件格式", "渲染器", "面数", "版本", "插件", "材质", "动画",
    "绑定", "贴图", "灯光", "模型编号", "分类", "风格", "单位",
)


@dataclass
class VideoItem:
    md5: str
    m3u8: str = ""
    poster: str = ""
    name: str = ""
    size_mb: str = ""


@dataclass
class PageData:
    url: str
    page_id: str = ""
    kind: str = "model"          # model | art
    title: str = ""
    author: str = ""
    keywords: List[str] = field(default_factory=list)
    description: str = ""        # 作者撰写的文字介绍
    params: Dict[str, str] = field(default_factory=dict)
    images: List[str] = field(default_factory=list)   # 大图 URL
    covers: List[str] = field(default_factory=list)   # 缩略图 URL
    video_md5: List[str] = field(default_factory=list)
    videos: List[VideoItem] = field(default_factory=list)

    def as_dict(self) -> dict:
        d = self.__dict__.copy()
        d["videos"] = [v.__dict__ for v in self.videos]
        return d


def normalize_url(u: str) -> str:
    u = u.strip()
    if u.startswith("//"):
        return "https:" + u
    return u


def cover_to_big(u: str) -> str:
    """/cover/ -> /big/ 得到站点最大可用尺寸。"""
    return re.sub(r"/cover/", "/big/", u)


def parse_id(url: str) -> tuple:
    m = re.search(r"/(model|art|works)/(\d+)", url)
    if not m:
        return "model", ""
    kind = "art" if m.group(1) in ("art", "works") else "model"
    return kind, m.group(2)


def _clean_text(t: str) -> str:
    lines = []
    for raw in t.splitlines():
        s = raw.strip()
        if not s or s in STOP_LABELS:
            continue
        if re.fullmatch(r"[（(]\s*\d+\s*[)）]|\d+|[(),（）]", s):
            continue
        if any(b in s for b in BOILERPLATE):
            continue
        if re.match(r"^\d+\.\s*$", s):
            continue
        lines.append(s)
    # 去重且保序
    out, seen = [], set()
    for s in lines:
        if s not in seen:
            seen.add(s)
            out.append(s)
    return "\n".join(out)


def _extract_images(soup: BeautifulSoup, html: str) -> tuple:
    covers: List[str] = []
    for li in soup.select("li.detail-thumb-item, .cgmodel-workdet-mthumb_item"):
        rel = li.get("data-rel") or ""
        if not rel:
            img = li.find("img")
            rel = (img.get("data-src") or img.get("src") or "") if img else ""
        if rel and IMG_HOST_RE.search(rel):
            covers.append(normalize_url(rel))

    # 兜底: 页面里所有 img.cgmodel.com 的 cover/big 资源
    if not covers:
        for m in re.finditer(r"(?://|https?://)img\.cgmodel\.com/image/[^\s\"'\\)]+?\.(?:webp|jpg|jpeg|png)", html):
            covers.append(normalize_url(m.group(0)))

    seen, uniq = set(), []
    for u in covers:
        k = re.sub(r"/(cover|big)/", "/", u)
        if k not in seen:
            seen.add(k)
            uniq.append(u)
    bigs = [cover_to_big(u) for u in uniq]
    return bigs, uniq


def _extract_params(soup: BeautifulSoup) -> Dict[str, str]:
    """站点参数有两种排版:

    1. ``.cgmodel-model-dr-parameter2-item`` —— 键值对
       (模型点数/模型面数/布线类型/发布时间...)
    2. ``.cgmodel-model-dr-parameter-item``  —— 布尔标签, 值体现在
       iconfont 图标类名: ...-cha = 无, ...-gou/-dui = 有
    """
    params: Dict[str, str] = {}

    for it in soup.select(".cgmodel-model-dr-parameter2-item, "
                          ".detail-parameter2-item"):
        spans = it.find_all("span")
        if len(spans) >= 2:
            key = spans[0].get_text(" ", strip=True)
            val = re.sub(r"\s+", " ", spans[-1].get_text(" ", strip=True))
            if key and val and key != val:
                params.setdefault(key, val)

    for it in soup.select(".cgmodel-model-dr-parameter-item, "
                          ".detail-parameter-item, .model-parameter-item"):
        sp = it.find("span")
        if not sp:
            continue
        key = sp.get_text(" ", strip=True)
        if not key or key in params:
            continue
        icon = it.find("i")
        cls = " ".join(icon.get("class", [])) if icon else ""
        if "-cha" in cls:
            params[key] = "无"
        elif "-gou" in cls or "-dui" in cls or "-que" in cls:
            params[key] = "有"
        else:
            rest = it.get_text(" ", strip=True)[len(key):].strip(" :：")
            params[key] = re.sub(r"\s+", " ", rest) or "-"

    # 作品页 / 通用的 "标签：值" 行
    for it in soup.select(".cgmodel-work-dcl-info li, .work-param-item"):
        txt = re.sub(r"\s+", " ", it.get_text(" ", strip=True))
        m = re.match(r"^([\u4e00-\u9fa5A-Za-z ]{2,10})\s*[:：]\s*(.+)$", txt)
        if m:
            params.setdefault(m.group(1).strip(), m.group(2).strip())
    return params


def _extract_description(soup: BeautifulSoup) -> str:
    """取作者撰写的说明。

    模型页: ``.model_explain`` 的直接文本 (兄弟 ``.explain-box`` 是站点
    固定协议模板, 需排除)。作品页: ``.cgmodel-work-dcl`` 内的描述块。
    """
    el = soup.select_one(".model_explain, #model_explain")
    if el:
        for box in el.select(".explain-box"):
            box.decompose()
        txt = _clean_text(el.get_text("\n", strip=True))
        if len(txt) >= 4:
            return txt

    for sel in (".detail-model-explain", ".cgmodel-work-dcl-desc",
                ".work-detail-content", ".cgmodel-work-dcl"):
        el = soup.select_one(sel)
        if not el:
            continue
        for box in el.select(".explain-box, .cgmodel-work-sz-share-list"):
            box.decompose()
        txt = _clean_text(el.get_text("\n", strip=True))
        if len(txt) >= 8:
            return txt

    meta = soup.find("meta", attrs={"name": "description"})
    return (meta.get("content", "").strip() if meta else "")


def parse_page(html: str, url: str) -> PageData:
    soup = BeautifulSoup(html, "lxml")
    for tag in soup(["script", "style", "noscript"]):
        tag.decompose()

    kind, pid = parse_id(url)
    data = PageData(url=url, page_id=pid, kind=kind)

    h1 = soup.find("h1")
    if h1:
        data.title = h1.get_text(" ", strip=True)
    if not data.title:
        t = soup.find("title")
        if t:
            data.title = re.split(r"\s*[-|]\s*", t.get_text(strip=True))[0]

    kw = soup.find("meta", attrs={"name": "keywords"})
    if kw:
        data.keywords = [k for k in re.split(r"[,，]", kw.get("content", "")) if k][:12]

    for sel in (".model-author-name", ".cgmodel-work-author-name",
                ".author-name", "a.user-name"):
        el = soup.select_one(sel)
        if el:
            data.author = el.get_text(strip=True)
            break

    data.images, data.covers = _extract_images(soup, html)
    data.params = _extract_params(soup)
    data.description = _extract_description(soup)
    data.video_md5 = sorted(set(re.findall(r'data-video=["\']([0-9a-f]{32})', html, re.I)))
    return data
