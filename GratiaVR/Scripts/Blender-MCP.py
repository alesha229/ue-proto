"""Evidence-recording client for the installed Blender Lab MCP HTTP server.

The live Blender session receives code only via MCP tools/call, rather than a
standalone Blender --python job. Input files avoid shell quoting ambiguities.
"""
import argparse
import json
import os
import pathlib
import urllib.request

BASE = "http://127.0.0.1:8100/"
OUT = pathlib.Path(__file__).resolve().parents[2] / "evidence" / "blender_mcp"
OUT.mkdir(parents=True, exist_ok=True)


def post(payload, session_id=None):
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if session_id:
        headers["mcp-session-id"] = session_id
    request = urllib.request.Request(BASE, json.dumps(payload).encode("utf-8"), headers, method="POST")
    with urllib.request.urlopen(request, timeout=int(os.environ.get("GRATIA_MCP_TIMEOUT", "240"))) as response:
        body = response.read().decode("utf-8", "replace")
        sid = response.headers.get("mcp-session-id") or session_id
        status = response.status
    result = None
    for line in body.splitlines():
        if line.startswith("data: "):
            result = json.loads(line[6:])
            break
    if result is None and body.strip():
        result = json.loads(body)
    return result, sid, status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["initialize", "list", "call", "code"])
    parser.add_argument("--name", required=True)
    parser.add_argument("--tool")
    parser.add_argument("--arguments-file")
    parser.add_argument("--code-file")
    parser.add_argument("--clip", choices=["Idle", "TestArms", "TestHead"])
    args = parser.parse_args()
    trace = {"endpoint": BASE, "action": args.action, "messages": []}
    init_payload = {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {
        "protocolVersion": "2025-03-26", "capabilities": {},
        "clientInfo": {"name": "Gratia-Blender-MCP-evidence", "version": "1.0"}}}
    init, sid, status = post(init_payload)
    trace["messages"].append({"request": init_payload, "response": init, "http_status": status})
    if not init or "error" in init:
        raise RuntimeError(f"MCP initialize failed: {init}")
    initialized = {"jsonrpc": "2.0", "method": "notifications/initialized"}
    _, _, initialized_status = post(initialized, sid)
    trace["messages"].append({"request": initialized, "http_status": initialized_status})
    response = init
    if args.action != "initialize":
        if args.action == "list":
            method, params = "tools/list", {}
        else:
            method = "tools/call"
            if args.action == "code":
                code = pathlib.Path(args.code_file).read_text(encoding="utf-8")
                if args.clip:
                    code = "CLIP = " + repr(args.clip) + "\n" + code
                params = {"name": "execute_blender_code", "arguments": {
                    "code": code}}
            else:
                arguments = json.loads(pathlib.Path(args.arguments_file).read_text(encoding="utf-8")) if args.arguments_file else {}
                params = {"name": args.tool, "arguments": arguments}
        payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
        response, _, status = post(payload, sid)
        trace["messages"].append({"request": payload, "response": response, "http_status": status})
    path = OUT / f"{args.name}.json"
    path.write_text(json.dumps(trace, ensure_ascii=False, indent=2), encoding="utf-8")
    structured = response.get("result", {}).get("structuredContent", {}) if response else {}
    if not response or "error" in response or response.get("result", {}).get("isError") or structured.get("status") == "error":
        raise RuntimeError(f"MCP call failed; see {path}")
    if args.action == "list":
        print(json.dumps({"evidence": str(path), "tools": [t["name"] for t in response["result"]["tools"]]}, indent=2))
    elif args.action == "initialize":
        print(json.dumps({"evidence": str(path), "server": init["result"]}, indent=2))
    else:
        print(json.dumps({"evidence": str(path), "result": response["result"]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
