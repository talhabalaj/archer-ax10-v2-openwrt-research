#!/usr/bin/env python3
"""Run bounded named-function analysis through a private REA stdio session.

Requires caller-configured GHIDRA_INSTALL_DIR, JAVA_HOME, and a supported Node.
Results contain raw analysis records and local paths: keep the output private.
No MCP/client configuration is changed and target bytes are never executed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--procedure", action="append", required=True)
    args = parser.parse_args()
    artifact = args.artifact.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    serial = 0
    with (output / "server.log").open("w") as log:
        server = subprocess.Popen(
            ["npx", "-y", "rea-agents@6.5.0", "mcp"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log,
            text=True, env=os.environ.copy(),
        )

        def call(method, params):
            nonlocal serial
            serial += 1
            server.stdin.write(json.dumps({"jsonrpc":"2.0", "id":serial, "method":method, "params":params}) + "\n")
            server.stdin.flush()
            while True:
                line = server.stdout.readline()
                if not line:
                    raise RuntimeError("REA server exited; inspect the private server log")
                record = json.loads(line)
                if record.get("id") == serial:
                    if "error" in record:
                        raise RuntimeError("REA protocol error; retain private diagnostics")
                    return record["result"]

        def tool(name, arguments, label):
            record = call("tools/call", {"name":name,"arguments":arguments})
            target = output / (label + ".json")
            target.write_text(json.dumps(record, indent=2) + "\n")
            target.chmod(0o600)
            if record.get("isError"):
                raise RuntimeError(f"REA {name} failed; inspect {target.name} privately")
            print(f"Completed {name}; private record: {target.name}", flush=True)
            return record

        opened = False
        try:
            call("initialize", {"protocolVersion":"2025-03-26", "capabilities":{}, "clientInfo":{"name":"ax10-driver-research","version":"1.0"}})
            server.stdin.write(json.dumps({"jsonrpc":"2.0", "method":"notifications/initialized"}) + "\n")
            server.stdin.flush()
            tool("binary_session", {"expected_package_version":"6.5.0"}, "session")
            tool("open_binary", {"path":str(artifact),"provider_id":"ghidra"}, "open")
            opened = True
            for index, procedure in enumerate(args.procedure):
                tool("search_procedures", {"pattern":procedure,"mode":"literal","case_sensitive":True}, f"search-{index}")
                tool("procedure_pseudo_code", {"procedure":procedure}, f"function-{index}")
        finally:
            try:
                if opened:
                    tool("close_binary", {}, "close")
            finally:
                server.stdin.close()
                try:
                    server.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait(timeout=10)


if __name__ == "__main__":
    main()
