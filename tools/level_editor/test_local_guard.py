"""The editor's server only answers requests the editor's own page could send (Host, Origin, Content-Type).

Needs no disc: the refusals happen before any handler runs, and the accepted case is /api/bye.
Run: python tools/level_editor/test_local_guard.py
"""
import http.client
import json
import os
import sys
import threading
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import editor  # noqa: E402


def main():
    class H(editor.Handler):
        pass
    H.cfg = {}
    srv = ThreadingHTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    port = srv.server_port
    own = "127.0.0.1:%d" % port

    def ask(method, path, body=b"{}", **headers):
        c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
        h = {"Host": own}
        h.update(headers)
        h = {k: v for k, v in h.items() if v is not None}
        c.putrequest(method, path, skip_host=True, skip_accept_encoding=True)
        for k, v in h.items():
            c.putheader(k, v)
        if method == "POST":
            c.putheader("Content-Length", str(len(body)))
        c.endheaders(body if method == "POST" else None)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, (json.loads(data) if data else {})

    # the page's own beacon (text/plain, same origin) and its JSON calls pass
    assert ask("POST", "/api/bye", **{"Content-Type": "text/plain;charset=UTF-8", "Origin": "http://" + own})[0] == 200
    assert ask("POST", "/api/bye", **{"Content-Type": "application/json", "Origin": "http://localhost:%d" % port,
                                       "Host": "localhost:%d" % port})[0] == 200
    assert ask("POST", "/api/bye", **{"Content-Type": "application/json", "Sec-Fetch-Site": "same-origin"})[0] == 200
    # Reload without saving needs no mod (the game reloads the file on disk): with no game it fails on that, not on the mod
    code, j = ask("POST", "/api/reload", b'{"name":"CENTRAL","mod":"","save":false}', **{"Content-Type": "application/json"})
    assert code == 400 and j.get("code") != "mod" and "no game" in j["error"], (code, j)
    code, j = ask("POST", "/api/reload", b'{"name":"CENTRAL","mod":"","save":true}', **{"Content-Type": "application/json"})
    assert code == 400 and j.get("code") == "mod", (code, j)
    # another site's page: no-cors fetch / form with a text/plain body, or any cross-site Origin
    code, j = ask("POST", "/api/config", b'{"game":"x.exe"}', **{"Content-Type": "text/plain", "Origin": "https://evil.example"})
    assert code == 403 and j["code"] == "forbidden", (code, j)
    assert ask("POST", "/api/config", b'{"game":"x.exe"}', **{"Content-Type": "text/plain"})[0] == 403      # not JSON
    assert ask("POST", "/api/config", b"{}", **{"Content-Type": "application/json", "Origin": "https://evil.example"})[0] == 403
    assert ask("POST", "/api/config", b"{}", **{"Content-Type": "application/json", "Sec-Fetch-Site": "cross-site"})[0] == 403
    assert ask("POST", "/api/bye", **{"Content-Type": "application/json", "Origin": "null"})[0] == 403
    # DNS rebinding: the name points here but the Host says otherwise; another port on this machine is another site
    assert ask("POST", "/api/bye", **{"Content-Type": "application/json", "Host": "evil.example:%d" % port})[0] == 403
    assert ask("GET", "/api/init", Host="rebind.example")[0] == 403
    assert ask("GET", "/api/init", Host="127.0.0.1:%d" % (port + 1))[0] == 403
    assert ask("GET", "/api/init", Host=None)[0] == 403
    assert ask("GET", "/api/init", Origin="https://evil.example")[0] == 403
    srv.shutdown()
    print("test_local_guard: ok")


if __name__ == "__main__":
    main()
