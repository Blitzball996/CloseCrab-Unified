import sublime
import sublime_plugin
import urllib.request
import json
import threading

SERVER_URL = "http://localhost:9001"


class ClosecrabChatCommand(sublime_plugin.WindowCommand):
    def run(self):
        self.window.show_input_panel("CloseCrab:", "", self.on_done, None, None)

    def on_done(self, text):
        if not text:
            return
        threading.Thread(target=self._send, args=(text,)).start()

    def _send(self, text):
        try:
            data = json.dumps({"message": text, "session_id": "sublime"}).encode()
            req = urllib.request.Request(
                f"{SERVER_URL}/chat",
                data=data,
                headers={"Content-Type": "application/json"},
            )
            resp = urllib.request.urlopen(req, timeout=120)
            result = json.loads(resp.read().decode())
            response = result.get("response", result.get("error", "No response"))
            sublime.set_timeout(lambda: self._show_result(response), 0)
        except Exception as e:
            sublime.set_timeout(
                lambda: sublime.status_message(f"CloseCrab Error: {e}"), 0
            )

    def _show_result(self, text):
        panel = self.window.create_output_panel("closecrab")
        panel.run_command("append", {"characters": text})
        self.window.run_command("show_panel", {"panel": "output.closecrab"})


class ClosecrabExplainCommand(sublime_plugin.TextCommand):
    def run(self, edit):
        selection = self.view.substr(self.view.sel()[0])
        if not selection:
            sublime.status_message("No text selected")
            return
        threading.Thread(target=self._send, args=(selection,)).start()

    def _send(self, code):
        try:
            msg = f"Explain this code:\n```\n{code}\n```"
            data = json.dumps({"message": msg, "session_id": "sublime"}).encode()
            req = urllib.request.Request(
                f"{SERVER_URL}/chat",
                data=data,
                headers={"Content-Type": "application/json"},
            )
            resp = urllib.request.urlopen(req, timeout=120)
            result = json.loads(resp.read().decode())
            response = result.get("response", "No response")
            sublime.set_timeout(lambda: self._show(response), 0)
        except Exception as e:
            sublime.set_timeout(
                lambda: sublime.status_message(f"Error: {e}"), 0
            )

    def _show(self, text):
        window = self.view.window()
        panel = window.create_output_panel("closecrab")
        panel.run_command("append", {"characters": text})
        window.run_command("show_panel", {"panel": "output.closecrab"})


class ClosecrabFixCommand(sublime_plugin.TextCommand):
    def run(self, edit):
        selection = self.view.substr(self.view.sel()[0])
        if not selection:
            return
        threading.Thread(target=self._send, args=(selection,)).start()

    def _send(self, code):
        try:
            msg = f"Fix bugs in this code, return only the fixed code:\n```\n{code}\n```"
            data = json.dumps({"message": msg, "session_id": "sublime"}).encode()
            req = urllib.request.Request(
                f"{SERVER_URL}/chat",
                data=data,
                headers={"Content-Type": "application/json"},
            )
            resp = urllib.request.urlopen(req, timeout=120)
            result = json.loads(resp.read().decode())
            response = result.get("response", "")
            sublime.set_timeout(lambda: self._show(response), 0)
        except Exception as e:
            sublime.set_timeout(
                lambda: sublime.status_message(f"Error: {e}"), 0
            )

    def _show(self, text):
        window = self.view.window()
        panel = window.create_output_panel("closecrab")
        panel.run_command("append", {"characters": text})
        window.run_command("show_panel", {"panel": "output.closecrab"})
