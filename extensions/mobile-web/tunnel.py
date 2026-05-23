"""
CloseCrab Remote Tunnel - 极简内网穿透
使用免费的 localhost.run 服务（只需要SSH）
或者使用 serveo.net

用法：
  python tunnel.py

它会输出一个公网URL，手机打开 URL/mobile 即可控制CloseCrab
"""
import subprocess
import sys
import os

def try_ssh_tunnel():
    """尝试用SSH隧道（localhost.run 免费服务）"""
    print("正在建立隧道到 localhost.run ...")
    print("（如果提示 yes/no 输入 yes）")
    print("")
    # localhost.run 免费提供SSH隧道
    cmd = ["ssh", "-R", "80:localhost:9001", "nokey@localhost.run"]
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in proc.stdout:
            print(line.strip())
            if "https://" in line:
                url = line.strip()
                print(f"\n{'='*50}")
                print(f"手机打开: {url}/mobile")
                print(f"{'='*50}\n")
    except FileNotFoundError:
        print("SSH未安装，尝试备用方案...")
        return False
    return True

def try_python_tunnel():
    """备用：用Python的pyngrok"""
    try:
        from pyngrok import ngrok
        tunnel = ngrok.connect(9001)
        print(f"\n{'='*50}")
        print(f"手机打开: {tunnel.public_url}/mobile")
        print(f"{'='*50}\n")
        print("按Ctrl+C停止")
        ngrok.kill()
    except ImportError:
        print("安装pyngrok中...")
        os.system(f"{sys.executable} -m pip install pyngrok")
        from pyngrok import ngrok
        tunnel = ngrok.connect(9001)
        print(f"\n{'='*50}")
        print(f"手机打开: {tunnel.public_url}/mobile")
        print(f"{'='*50}\n")
        input("按回车停止...")
        ngrok.kill()

if __name__ == "__main__":
    print("CloseCrab Remote Tunnel")
    print("确保 closecrab-unified.exe 已在运行\n")

    if not try_ssh_tunnel():
        try_python_tunnel()
