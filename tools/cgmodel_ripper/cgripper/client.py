"""HTTP 会话封装：UA 伪装、Referer、CSRF (PHPSESSScrf) 与重试。"""

from __future__ import annotations

import json
import time
from typing import Any, Dict, List, Optional

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

SITE = "https://www.cgmodel.com"

UA = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
)

# 站点用于取视频真实地址的内部接口
AJAX_ENDPOINT = "/works/index-out-init.html"
ACTION_VIDEO = "model-GetVideoAll"


class CgClient:
    """带 CSRF 与重试的会话。首次访问站点即可拿到 PHPSESSScrf cookie。"""

    def __init__(self, timeout: int = 30, proxy: Optional[str] = None,
                 cookie: Optional[str] = None) -> None:
        self.timeout = timeout
        self.s = requests.Session()
        self.s.headers.update({
            "User-Agent": UA,
            "Accept-Language": "zh-CN,zh;q=0.9,en;q=0.8",
            "Referer": SITE + "/",
        })
        if proxy:
            self.s.proxies.update({"http": proxy, "https": proxy})
        if cookie:
            self.s.headers["Cookie"] = cookie

        retry = Retry(total=3, backoff_factor=0.6,
                      status_forcelist=(429, 500, 502, 503, 504),
                      allowed_methods=frozenset({"GET", "POST"}))
        ad = HTTPAdapter(max_retries=retry, pool_maxsize=32)
        self.s.mount("http://", ad)
        self.s.mount("https://", ad)
        self._warm = False

    # ---------- 基础 ----------

    def warmup(self) -> None:
        """访问首页以获取 session/CSRF cookie。"""
        if self._warm:
            return
        try:
            self.s.get(SITE + "/", timeout=self.timeout)
        except requests.RequestException:
            pass
        self._warm = True

    @property
    def scrf(self) -> str:
        return self.s.cookies.get("PHPSESSScrf") or ""

    def get_text(self, url: str, referer: Optional[str] = None) -> str:
        self.warmup()
        h = {"Referer": referer} if referer else {}
        r = self.s.get(url, headers=h, timeout=self.timeout)
        r.raise_for_status()
        r.encoding = r.apparent_encoding or "utf-8"
        return r.text

    def get_bytes(self, url: str, referer: str = SITE + "/") -> bytes:
        self.warmup()
        r = self.s.get(url, headers={"Referer": referer}, timeout=self.timeout)
        r.raise_for_status()
        return r.content

    # ---------- 视频接口 ----------

    def fetch_videos(self, md5_list: List[str], referer: str) -> List[Dict[str, Any]]:
        """用页面中的 data-video md5 换取 m3u8 / 封面 / 文件名。"""
        if not md5_list:
            return []
        self.warmup()
        payload = {
            "scrfCode": self.scrf,
            "arrFileMd5": json.dumps(md5_list),
            "action": ACTION_VIDEO,
            "isCover": 2,
        }
        headers = {"Referer": referer, "X-Requested-With": "XMLHttpRequest"}
        for attempt in range(3):
            try:
                r = self.s.post(SITE + AJAX_ENDPOINT, data=payload,
                                headers=headers, timeout=self.timeout)
                r.raise_for_status()
                data = json.loads(r.text)
                return list(data.get("listVideo") or [])
            except (requests.RequestException, ValueError):
                if attempt == 2:
                    return []
                time.sleep(1 + attempt)
        return []

    def close(self) -> None:
        self.s.close()

    def __enter__(self) -> "CgClient":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()
